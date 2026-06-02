-- watch_menu.lua (Snes9x 1.6x) — WATCH-ONLY ground truth for SMK's menu entry.
--
-- Minimal/robust: no input injection (YOU press SELECT+START at the title), just
-- prints $36/$32/$0E68/$1040 every time they change, plus the controller. This
-- shows whether real SMK opens the title input gate $0E68 and what enters mode
-- select ($32=$0014) when you press SELECT+START.
--
-- RUN: Snes9x -> File -> Lua Scripting -> New Lua Script Window -> Browse to this
--      -> Run (with SMK loaded). Watch the Lua console; also writes
--      smk_menu_snes9x.log next to snes9x. Then press SELECT+START at the title.

local f = io.open("smk_menu_snes9x.log", "w")
local function out(s) print(s); if f then f:write(s.."\n"); f:flush() end end

-- WRAM read. Snes9x lua maps WRAM at 0x7E0000.. ; if a build wants bank-0 low RAM
-- ($0036), 0x7E0036 still resolves. readword = little-endian 16-bit.
local function w(a) return memory.readword(a) end
local function line(tag)
  return string.format("%s $36=%04X $32=%04X $0E68=%04X $1040=%04X  pad=$0020:%04X",
    tag, w(0x7E0036), w(0x7E0032), w(0x7E0E68), w(0x7E1040), w(0x7E0020))
end

out("WATCH_MENU (snes9x) running — press SELECT+START at the title.")
out(line("[init]"))

local frame, l36, l32, l68 = 0, -1, -1, -1
local function tick()
  frame = frame + 1
  local s36, s32, s68 = w(0x7E0036), w(0x7E0032), w(0x7E0E68)
  if s36 ~= l36 or s32 ~= l32 or s68 ~= l68 then
    out(line(string.format("[f%d]", frame)))
    l36, l32, l68 = s36, s32, s68
  end
end

-- Per-frame hook (snes9x: emu.registerafter; older builds: gui.register).
if emu and emu.registerafter then emu.registerafter(tick)
elseif gui and gui.register then gui.register(tick)
else out("ERROR: no per-frame hook (emu.registerafter/gui.register) in this build") end
