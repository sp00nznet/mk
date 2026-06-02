-- watch_menu.lua — observe SMK's real title->menu entry on ground-truth Mesen2.
--
-- WATCH-ONLY (no input injection, so nothing can error): it logs every write to
-- the key flow vars with the CPU PC + a state snapshot. YOU drive: at the title,
-- press START (and try SELECT+START); the log captures exactly what enters mode
-- select ($32=$0014) and whether the title input gate $0E68 opens, and the PC
-- that did it.
--
-- HOW TO RUN (Mesen 2.1.1; needs Firmware\dsp1b.rom present):
--   GUI:  load "Super Mario Kart (USA).sfc" -> Debug -> Script Window ->
--         open this file -> Run. Watch the log console (and smk_menu.log).
--         Then PLAY: press Start at the title, navigate the menus.
--
-- Output goes to the Script Window console (emu.log) AND smk_menu.log in Mesen's
-- home folder. Paste it back and we trace the menu entry from there.

local f = io.open("smk_menu.log", "w")
local function out(s) emu.log(s); if f then f:write(s .. "\n"); f:flush() end end
local function rd(a) return emu.read(a, emu.memType.snesMemory) end
local function rd16(a) return rd(a) | (rd(a + 1) << 8) end
local function pcstr() local c = emu.getState().cpu; return string.format("%02X:%04X", c.k or 0, c.pc or 0) end
local function snap() return string.format("$36=%04X $32=%04X $0E68=%04X $0160=%04X",
  rd16(0x7E0036), rd16(0x7E0032), rd16(0x7E0E68), rd16(0x7E0160)) end

out("WATCH_MENU running — now press START at the title (then try SELECT+START).")

local frame = 0

-- Writes to the flow vars, with the PC that did it.
local function watch(off, name)
  emu.addMemoryCallback(function(addr, v)
    out(string.format("[f%d] WR %-12s <- %02X  pc=%s   (%s)", frame, name, v, pcstr(), snap()))
  end, emu.callbackType.write, 0x7E0000 + off, 0x7E0000 + off)
end
watch(0x36, "$36 state")
watch(0x32, "$32 next")
watch(0xE68, "$0E68 gate")

-- Per-frame: count frames + log $36/$32 transitions (and a heartbeat).
local l36, l32 = -1, -1
emu.addEventCallback(function()
  frame = frame + 1
  local s36, s32 = rd16(0x7E0036), rd16(0x7E0032)
  if s36 ~= l36 or s32 ~= l32 then
    out(string.format("[f%d] STATE %s", frame, snap()))
    l36, l32 = s36, s32
  end
  if frame % 600 == 0 then out(string.format("[f%d] ... still watching (%s)", frame, snap())) end
end, emu.eventType.endFrame)
