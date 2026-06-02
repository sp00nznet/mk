-- watch_menu.lua — find SMK's real title->menu entry on ground-truth Mesen2.
--
-- SELF-DRIVING (no human input): it auto-presses START, then SELECT+START, over
-- the title and logs every write to the key flow vars with the CPU PC, so we can
-- see exactly what enters the menu and whether the $0E68 title-input gate opens.
-- Writes smk_menu.log (in Mesen's LuaScriptData folder) and stops itself.
--
-- HOW TO RUN
--   GUI (reliable):  Mesen2 -> load "Super Mario Kart (USA).sfc"
--                    -> Debug -> Script Window -> open this file -> Run.
--                    It auto-drives, writes smk_menu.log, and stops.
--   Headless (try):  Mesen.exe "Super Mario Kart (USA).sfc" --testRunner ^
--                      --luaScript=tools\mesen\watch_menu.lua
--                    (In this repo's Mesen 2.1.1 the headless runner did NOT
--                     execute scripts; if yours is the same, use the GUI method.)
--
-- WHAT IT TESTS (SMK title settles ~f1080; demo auto-starts ~f1900):
--   Phase A f1150-1230: hold START alone            (plain "PUSH START")
--   Phase B f1500-1580: hold SELECT+START together  (the Select-gated combo at
--                                                     $80:8559/$8560)
-- A win = $32 becomes $0014 (mode select) and/or $36 leaves $0004. The log shows
-- the PC that wrote it and whether $0E68 opened first.
-- Paste smk_menu.log back and we trace from there.

local MAX_FRAMES = 2000

-- {firstFrame, lastFrame, label, buttonTable}
local PHASES = {
  { 1150, 1230, "START",        { start = true } },
  { 1500, 1580, "SELECT+START", { select = true, start = true } },
}

local frame = 0
local f = io.open("smk_menu.log", "w")   -- relative -> Mesen's LuaScriptData dir
local function out(s) emu.log(s); if f then f:write(s .. "\n"); f:flush() end end
local function rd(a) return emu.read(a, emu.memType.snesMemory) end
local function rd16(a) return rd(a) | (rd(a + 1) << 8) end
local function pcstr() local c = emu.getState().cpu; return string.format("%02X:%04X", c.k or 0, c.pc or 0) end

out("WATCH_MENU loaded (log open=" .. tostring(f ~= nil) .. ")")

-- Log writes to the flow vars with PC + a snapshot of all four.
local function watch(off, name)
  emu.addMemoryCallback(function(addr, v)
    out(string.format("[f%d] WR %-14s <- %02X  pc=%s   ($36=%04X $32=%04X $0E68=%04X $0160=%04X)",
      frame, name, v, pcstr(), rd16(0x7E0036), rd16(0x7E0032), rd16(0x7E0E68), rd16(0x7E0160)))
  end, emu.callbackType.write, 0x7E0000 + off, 0x7E0000 + off)
end
watch(0x36, "$36 state")
watch(0x32, "$32 nextmode")
watch(0xE68, "$0E68 gate")

-- Inject input at the poll point (the correct place for emu.setInput).
local cur_phase = 0
emu.addEventCallback(function()
  local btn
  for i, p in ipairs(PHASES) do
    if frame >= p[1] and frame <= p[2] then
      btn = p[4]
      if cur_phase ~= i then cur_phase = i
        out(string.format("[f%d] >>> PHASE %d: holding %s", frame, i, p[3])) end
    end
  end
  if btn then emu.setInput(0, btn) end
end, emu.eventType.inputPolled)

-- Per-frame: log $36/$32 transitions; end + summarize.
local l36, l32 = -1, -1
emu.addEventCallback(function()
  frame = frame + 1
  local s36, s32 = rd16(0x7E0036), rd16(0x7E0032)
  if s36 ~= l36 or s32 ~= l32 then
    out(string.format("[f%d] STATE $36=%04X $32=%04X  $0E68=%04X", frame, s36, s32, rd16(0x7E0E68)))
    l36, l32 = s36, s32
  end
  if frame >= MAX_FRAMES then
    out(string.format("WATCH_MENU DONE f=%d  final $36=%04X $32=%04X $0E68=%04X",
      frame, s36, s32, rd16(0x7E0E68)))
    if f then f:close() end
    emu.stop(0)
  end
end, emu.eventType.endFrame)
