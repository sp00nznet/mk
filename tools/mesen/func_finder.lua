-- func_finder.lua — Mesen2 Lua script to identify function boundaries
--
-- Watches for JSR/JSL/RTS/RTL instructions to build a call graph.
-- Outputs a JSON-style function map for the Python recompiler.
--
-- Usage: Load in Mesen2 Script Window while playing SMK.

local OUTPUT_FILE = "smk_functions.json"
local MAX_FRAMES = 1800  -- 30 seconds

-- Track function entries (targets of JSR/JSL) and returns
local functions = {}    -- addr -> { calls_from = {}, call_count = N }
local jsr_sites = {}    -- addr -> target (JSR/JSL source -> destination)
local frame_count = 0

-- 65816 opcodes that are calls/returns
local JSR_ABS  = 0x20   -- JSR abs
local JSL_LONG = 0x22   -- JSL long
local JSR_IND  = 0xFC   -- JSR (abs,X)
local RTS      = 0x60
local RTL      = 0x6B

local function on_exec(addr, value)
    local opcode = emu.read(addr, emu.memType.snesMemory)

    if opcode == JSR_ABS then
        -- JSR $XXXX — target is in same bank
        local lo = emu.read(addr + 1, emu.memType.snesMemory)
        local hi = emu.read(addr + 2, emu.memType.snesMemory)
        local bank = (addr >> 16) & 0xFF
        local target = (bank << 16) | (hi << 8) | lo

        if not functions[target] then
            functions[target] = { call_count = 0, callers = {} }
        end
        functions[target].call_count = functions[target].call_count + 1
        functions[target].callers[addr] = true

    elseif opcode == JSL_LONG then
        -- JSL $XXXXXX — target is a full 24-bit address
        local lo = emu.read(addr + 1, emu.memType.snesMemory)
        local hi = emu.read(addr + 2, emu.memType.snesMemory)
        local bk = emu.read(addr + 3, emu.memType.snesMemory)
        local target = (bk << 16) | (hi << 8) | lo

        if not functions[target] then
            functions[target] = { call_count = 0, callers = {} }
        end
        functions[target].call_count = functions[target].call_count + 1
        functions[target].callers[addr] = true
    end
end

local function on_frame()
    frame_count = frame_count + 1
    if frame_count % 60 == 0 then
        local count = 0
        for _ in pairs(functions) do count = count + 1 end
        emu.log(string.format("Frame %d — %d unique function targets found", frame_count, count))
    end

    if frame_count >= MAX_FRAMES then
        -- Write output
        local f = io.open(OUTPUT_FILE, "w")
        if f then
            f:write("{\n")
            local first = true
            for target, info in pairs(functions) do
                if not first then f:write(",\n") end
                first = false

                -- Collect callers
                local caller_list = {}
                for caller_addr, _ in pairs(info.callers) do
                    table.insert(caller_list, string.format("\"$%06X\"", caller_addr))
                end

                f:write(string.format(
                    "  \"$%06X\": { \"call_count\": %d, \"callers\": [%s] }",
                    target, info.call_count, table.concat(caller_list, ", ")
                ))
            end
            f:write("\n}\n")
            f:close()
            emu.log(string.format("Function map written → %s", OUTPUT_FILE))
        else
            emu.log("ERROR: Could not write " .. OUTPUT_FILE)
        end
        emu.stop()
    end
end

emu.addMemoryCallback(on_exec, emu.callbackType.exec, 0x000000, 0xFFFFFF)
emu.addEventCallback(on_frame, emu.eventType.endFrame)
