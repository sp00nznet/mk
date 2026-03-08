-- hw_logger.lua — Mesen2 Lua script to log hardware register access patterns
--
-- Captures all reads/writes to PPU ($2100-$213F), APU ($2140-$2143),
-- CPU I/O ($4200-$42FF), and DMA ($4300-$437F) registers.
-- This helps identify which HAL functions need real implementations.
--
-- Usage: Load in Mesen2 Script Window while playing SMK.

local OUTPUT_FILE = "smk_hw_access.log"
local MAX_FRAMES = 600  -- 10 seconds

local hw_writes = {}  -- addr -> { values = {}, callers = {}, count = N }
local hw_reads = {}   -- addr -> { callers = {}, count = N }
local frame_count = 0

-- Register name lookup tables
local ppu_regs = {
    [0x2100] = "INIDISP",  [0x2101] = "OBSEL",    [0x2102] = "OAMADDL",
    [0x2103] = "OAMADDH",  [0x2104] = "OAMDATA",  [0x2105] = "BGMODE",
    [0x2106] = "MOSAIC",   [0x2107] = "BG1SC",    [0x2108] = "BG2SC",
    [0x2109] = "BG3SC",    [0x210A] = "BG4SC",    [0x210B] = "BG12NBA",
    [0x210C] = "BG34NBA",  [0x210D] = "BG1HOFS",  [0x210E] = "BG1VOFS",
    [0x210F] = "BG2HOFS",  [0x2110] = "BG2VOFS",  [0x2111] = "BG3HOFS",
    [0x2112] = "BG3VOFS",  [0x2113] = "BG4HOFS",  [0x2114] = "BG4VOFS",
    [0x2115] = "VMAIN",    [0x2116] = "VMADDL",   [0x2117] = "VMADDH",
    [0x2118] = "VMDATAL",  [0x2119] = "VMDATAH",  [0x211A] = "M7SEL",
    [0x211B] = "M7A",      [0x211C] = "M7B",      [0x211D] = "M7C",
    [0x211E] = "M7D",      [0x211F] = "M7X",      [0x2120] = "M7Y",
    [0x2121] = "CGADD",    [0x2122] = "CGDATA",   [0x2123] = "W12SEL",
    [0x2124] = "W34SEL",   [0x2125] = "WOBJSEL",  [0x2126] = "WH0",
    [0x2127] = "WH1",      [0x2128] = "WH2",      [0x2129] = "WH3",
    [0x212A] = "WBGLOG",   [0x212B] = "WOBJLOG",  [0x212C] = "TM",
    [0x212D] = "TS",       [0x212E] = "TMW",      [0x212F] = "TSW",
    [0x2130] = "CGWSEL",   [0x2131] = "CGADSUB",  [0x2132] = "COLDATA",
    [0x2133] = "SETINI",   [0x2134] = "MPYL",     [0x2135] = "MPYM",
    [0x2136] = "MPYH",     [0x2137] = "SLHV",     [0x2138] = "RDOAM",
    [0x2139] = "RDVRAML",  [0x213A] = "RDVRAMH",  [0x213B] = "RDCGRAM",
    [0x213C] = "OPHCT",    [0x213D] = "OPVCT",    [0x213E] = "STAT77",
    [0x213F] = "STAT78",
}

local io_regs = {
    [0x4200] = "NMITIMEN",  [0x4201] = "WRIO",     [0x4202] = "WRMPYA",
    [0x4203] = "WRMPYB",    [0x4204] = "WRDIVL",   [0x4205] = "WRDIVH",
    [0x4206] = "WRDIVB",    [0x4207] = "HTIMEL",   [0x4208] = "HTIMEH",
    [0x4209] = "VTIMEL",    [0x420A] = "VTIMEH",    [0x420B] = "MDMAEN",
    [0x420C] = "HDMAEN",    [0x420D] = "MEMSEL",    [0x4210] = "RDNMI",
    [0x4211] = "TIMEUP",    [0x4212] = "HVBJOY",    [0x4213] = "RDIO",
    [0x4214] = "RDDIVL",    [0x4215] = "RDDIVH",    [0x4216] = "RDMPYL",
    [0x4217] = "RDMPYH",    [0x4218] = "JOY1L",     [0x4219] = "JOY1H",
    [0x421A] = "JOY2L",     [0x421B] = "JOY2H",     [0x421C] = "JOY3L",
    [0x421D] = "JOY3H",     [0x421E] = "JOY4L",     [0x421F] = "JOY4H",
}

local function get_reg_name(addr)
    if ppu_regs[addr] then return ppu_regs[addr] end
    if io_regs[addr] then return io_regs[addr] end
    if addr >= 0x2140 and addr <= 0x2143 then
        return string.format("APUIO%d", addr - 0x2140)
    end
    if addr >= 0x4300 and addr <= 0x437F then
        local ch = (addr >> 4) & 0x07
        local reg = addr & 0x0F
        local dma_names = {
            [0] = "DMAPx", [1] = "BBADx", [2] = "A1TxL", [3] = "A1TxH",
            [4] = "A1Bx",  [5] = "DASxL", [6] = "DASxH", [7] = "DASBx",
            [8] = "A2AxL", [9] = "A2AxH", [0xA] = "NTRLx", [0xB] = "UNUSEDx",
        }
        local name = dma_names[reg] or string.format("DMA_%X", reg)
        return string.format("%s[%d]", name, ch)
    end
    return string.format("$%04X", addr)
end

local function on_write(addr, value)
    local reg = addr & 0xFFFF  -- strip bank for register address
    if not hw_writes[reg] then
        hw_writes[reg] = { values = {}, count = 0 }
    end
    local entry = hw_writes[reg]
    entry.count = entry.count + 1
    entry.values[value] = (entry.values[value] or 0) + 1
end

local function on_read(addr, value)
    local reg = addr & 0xFFFF
    if not hw_reads[reg] then
        hw_reads[reg] = { count = 0 }
    end
    hw_reads[reg].count = hw_reads[reg].count + 1
end

local function on_frame()
    frame_count = frame_count + 1
    if frame_count >= MAX_FRAMES then
        local f = io.open(OUTPUT_FILE, "w")
        if not f then
            emu.log("ERROR: Could not open " .. OUTPUT_FILE)
            emu.stop()
            return
        end

        f:write("# SMK Hardware Register Access Log\n")
        f:write(string.format("# Captured over %d frames\n\n", frame_count))

        -- Sort by address
        local write_addrs = {}
        for addr, _ in pairs(hw_writes) do table.insert(write_addrs, addr) end
        table.sort(write_addrs)

        local read_addrs = {}
        for addr, _ in pairs(hw_reads) do table.insert(read_addrs, addr) end
        table.sort(read_addrs)

        f:write("=== WRITES ===\n")
        f:write(string.format("%-10s %-12s %8s  %s\n", "Address", "Register", "Count", "Values Written"))
        f:write(string.rep("-", 80) .. "\n")
        for _, addr in ipairs(write_addrs) do
            local entry = hw_writes[addr]
            local vals = {}
            for v, cnt in pairs(entry.values) do
                table.insert(vals, string.format("$%02X(%d)", v, cnt))
            end
            -- Only show first 8 unique values
            local val_str = table.concat(vals, " ", 1, math.min(#vals, 8))
            if #vals > 8 then val_str = val_str .. " ..." end
            f:write(string.format("$%04X     %-12s %8d  %s\n",
                addr, get_reg_name(addr), entry.count, val_str))
        end

        f:write("\n=== READS ===\n")
        f:write(string.format("%-10s %-12s %8s\n", "Address", "Register", "Count"))
        f:write(string.rep("-", 50) .. "\n")
        for _, addr in ipairs(read_addrs) do
            local entry = hw_reads[addr]
            f:write(string.format("$%04X     %-12s %8d\n",
                addr, get_reg_name(addr), entry.count))
        end

        f:close()
        emu.log(string.format("HW access log written → %s (%d write regs, %d read regs)",
            OUTPUT_FILE, #write_addrs, #read_addrs))
        emu.stop()
    end
end

-- Register callbacks for hardware register ranges
-- PPU $2100-$213F
emu.addMemoryCallback(on_write, emu.callbackType.write, 0x002100, 0x00213F)
emu.addMemoryCallback(on_read,  emu.callbackType.read,  0x002100, 0x00213F)
-- APU I/O $2140-$2143
emu.addMemoryCallback(on_write, emu.callbackType.write, 0x002140, 0x002143)
emu.addMemoryCallback(on_read,  emu.callbackType.read,  0x002140, 0x002143)
-- CPU I/O $4200-$42FF
emu.addMemoryCallback(on_write, emu.callbackType.write, 0x004200, 0x0042FF)
emu.addMemoryCallback(on_read,  emu.callbackType.read,  0x004200, 0x0042FF)
-- DMA $4300-$437F
emu.addMemoryCallback(on_write, emu.callbackType.write, 0x004300, 0x00437F)
emu.addMemoryCallback(on_read,  emu.callbackType.read,  0x004300, 0x00437F)

emu.addEventCallback(on_frame, emu.eventType.endFrame)
emu.log("HW logger started — capturing register access patterns...")
