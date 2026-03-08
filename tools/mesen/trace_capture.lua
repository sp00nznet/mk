-- trace_capture.lua — Mesen2 Lua script for Super Mario Kart trace capture
--
-- Usage: Load in Mesen2 Script Window, or run headless:
--   Mesen.exe --testrunner "Super Mario Kart (USA).sfc" --lua trace_capture.lua
--
-- Outputs a structured trace log to 'smk_trace.log' in the working directory.
-- Captures: PC (24-bit), opcode bytes, disassembly, register state, P flags.

local TRACE_FILE = "smk_trace.log"
local MAX_FRAMES = 600        -- ~10 seconds at 60fps, adjust as needed
local LOG_UNIQUE_ONLY = true  -- only log first visit to each PC address

-- Track unique addresses for code coverage
local visited = {}
local unique_count = 0
local frame_count = 0

-- Open output file
local f = io.open(TRACE_FILE, "w")
if not f then
    emu.log("ERROR: Could not open " .. TRACE_FILE)
    return
end

-- Write header
f:write("# SMK Trace Capture — Mesen2\n")
f:write("# Format: PC      | ByteCode | Disassembly                      | A    X    Y    SP   DB DP   P    | Flags\n")
f:write("# " .. string.rep("-", 100) .. "\n")

emu.log("Trace capture started → " .. TRACE_FILE)

-- Callback: fires on every CPU instruction execution
local function on_exec(addr, value)
    local pc_full = addr  -- 24-bit address from Mesen2

    if LOG_UNIQUE_ONLY then
        if visited[pc_full] then return end
        visited[pc_full] = true
        unique_count = unique_count + 1
    end

    local state = emu.getState()
    local cpu = state.cpu

    -- Extract P flags into a readable string
    local p = cpu.ps
    local flags = ""
    flags = flags .. (((p & 0x80) ~= 0) and "N" or "n")
    flags = flags .. (((p & 0x40) ~= 0) and "V" or "v")
    flags = flags .. (((p & 0x20) ~= 0) and "M" or "m")  -- 8-bit accum
    flags = flags .. (((p & 0x10) ~= 0) and "X" or "x")  -- 8-bit index
    flags = flags .. (((p & 0x08) ~= 0) and "D" or "d")
    flags = flags .. (((p & 0x04) ~= 0) and "I" or "i")
    flags = flags .. (((p & 0x02) ~= 0) and "Z" or "z")
    flags = flags .. (((p & 0x01) ~= 0) and "C" or "c")

    -- Read up to 4 bytes at PC for opcode encoding
    local b0 = emu.read(pc_full, emu.memType.snesMemory)
    local b1 = emu.read(pc_full + 1, emu.memType.snesMemory)
    local b2 = emu.read(pc_full + 2, emu.memType.snesMemory)
    local b3 = emu.read(pc_full + 3, emu.memType.snesMemory)

    local line = string.format(
        "%02X:%04X  %02X %02X %02X %02X  A:%04X X:%04X Y:%04X SP:%04X DB:%02X DP:%04X P:%02X  %s\n",
        (pc_full >> 16) & 0xFF, pc_full & 0xFFFF,
        b0, b1, b2, b3,
        cpu.a, cpu.x, cpu.y, cpu.sp,
        cpu.db, cpu.d, p,
        flags
    )
    f:write(line)
end

-- Callback: fires once per frame (end of VBlank)
local function on_frame()
    frame_count = frame_count + 1
    if frame_count % 60 == 0 then
        emu.log(string.format("Frame %d — %d unique addresses captured", frame_count, unique_count))
    end
    if frame_count >= MAX_FRAMES then
        f:write(string.format("# Capture ended: %d frames, %d unique addresses\n", frame_count, unique_count))
        f:close()
        emu.log(string.format("Trace capture complete: %d frames, %d unique addresses → %s", frame_count, unique_count, TRACE_FILE))
        emu.stop()
    end
end

-- Register callbacks
emu.addMemoryCallback(on_exec, emu.callbackType.exec, 0x000000, 0xFFFFFF)
emu.addEventCallback(on_frame, emu.eventType.endFrame)
