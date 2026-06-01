-- dump_snapshot.lua — Mesen2 ground-truth snapshot for the lockstep harness.
--
-- Emits a binary file in the EXACT format snesrecomp_dump_snapshot writes
-- (magic "SMKSNAP2"), so tools/diff_snapshots.py can diff snesrecomp's PPU
-- state against a known-good emulator. Use this to isolate the garbled Mode-7
-- track: if VRAM/CGRAM differ from Mesen2, the fault is in snesrecomp's
-- frame/DMA upload model; if they match, it's a renderer/matrix fault.
--
-- Usage in Mesen2:
--   1. Load Super Mario Kart, let the attract demo reach the Mode-7 race
--      (or pause on any Mode-7 frame you want to compare).
--   2. Debug -> Script Window -> load this file -> Run.
--   3. It dumps once at frame DUMP_FRAME to OUT_PATH.
--   4. Compare:  py tools/diff_snapshots.py mesen_m7.bin build/recomp_m7.bin --vram-only
--
-- Tune DUMP_FRAME so it lands on the same Mode-7 frame as the snesrecomp dump.

local DUMP_FRAME = 1800
local OUT_PATH   = "mesen_m7.bin"

local WRAM_SIZE  = 0x20000
local VRAM_SIZE  = 0x10000  -- bytes (0x8000 words)
local CGRAM_SIZE = 0x200    -- bytes (0x100 words)

local frame_count = 0
local dumped = false

local function u32le(v)
  return string.char(v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff)
end

local function dump_block(f, memType, size)
  -- Write bytes in chunks to keep the Lua string buffer reasonable.
  local CHUNK = 0x1000
  local i = 0
  while i < size do
    local n = math.min(CHUNK, size - i)
    local parts = {}
    for j = 0, n - 1 do
      parts[j + 1] = string.char(emu.read(i + j, memType) & 0xff)
    end
    f:write(table.concat(parts))
    i = i + n
  end
end

emu.addEventCallback(function()
  frame_count = frame_count + 1
  if frame_count == DUMP_FRAME and not dumped then
    dumped = true
    local f = io.open(OUT_PATH, "wb")
    if not f then
      emu.log("dump_snapshot: cannot open " .. OUT_PATH)
      return
    end
    f:write("SMKSNAP2")
    f:write(u32le(WRAM_SIZE))
    f:write(u32le(VRAM_SIZE))
    f:write(u32le(CGRAM_SIZE))
    dump_block(f, emu.memType.snesWorkRam, WRAM_SIZE)
    dump_block(f, emu.memType.snesVideoRam, VRAM_SIZE)
    dump_block(f, emu.memType.snesCgRam, CGRAM_SIZE)
    f:close()
    emu.log(string.format("dump_snapshot: wrote %s at frame %d", OUT_PATH, frame_count))
  end
end, emu.eventType.endFrame)

emu.log(string.format("dump_snapshot loaded. Will dump %s at frame %d.", OUT_PATH, DUMP_FRAME))
