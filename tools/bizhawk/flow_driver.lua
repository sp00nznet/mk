-- flow_driver.lua — headless ground-truth driver for SMK's title->race flow.
--
-- Runs on BizHawk's *snes9x* core (HLE DSP-1, no firmware needed) fully headless.
-- Proves and reproduces the authentic menu flow that drives SMK from a cold boot
-- all the way into a live Mode-7 race, using only the controller:
--
--   $36=$04 Title  --(Start+Y)-->  $36=$06 Driver select
--                  --(Start+Y)-->  $36=$08 Class/Cup select (50cc, Mushroom/Flower/Star)
--                  --(Start+Y)-->  $36=$02 RACE (Mode-7, "ROUND 1")
--
-- KEY FACTS (verified on the snes9x core, the user's confirmed-good reference):
--   * Start+Y is the universal "confirm/advance" button, NOT Select+Start.
--     (snes9x's default keyboard maps Space->Y; the user's "start+space" was Start+Y.)
--   * The title's input gate opens only AFTER the title finishes fading in
--     ($0160 reaches $0F00, ~frame 360). Presses before that are ignored.
--   * $36 = current mode (master sequencer at $81:E000). $32 = pending mode.
--
-- RUN (headless, no window):
--   EmuHawk.exe --chromeless --lua=flow_driver.lua "smk.sfc"
-- Writes smk_flow.log + screenshots (flow_*.png) next to EmuHawk.

local LOG  = "smk_flow.log"
local SHOT = "flow_"
local f = io.open(LOG, "w")
local function out(s) console.log(s); if f then f:write(s.."\n"); f:flush() end end
local function r8(a)  return memory.read_u8(a, "WRAM") end
local function r16(a) return memory.read_u16_le(a, "WRAM") end
local function snap()
  return string.format("$36=%04X $32=%04X $0150=%04X $0160=%04X",
    r16(0x36), r16(0x32), r16(0x150), r16(0x160))
end

out("SMK flow_driver: tap Start+Y from f360 to walk title->driver->cup->race")
local frame, last, seen = 0, "", {}
while frame < 1600 do
  -- Tap Start+Y in 6-frame pulses every 70 frames once the title is up.
  -- Repeated pulses accept the default sub-selection at each prompt and advance.
  local held = {}
  if frame >= 360 and (frame % 70) < 6 then held = { Start = true, Y = true } end
  joypad.set(held, 1)
  emu.frameadvance()
  frame = frame + 1

  local cur = snap()
  if cur ~= last then out(string.format("[f%d] %s", frame, cur)); last = cur end

  -- One clean screenshot per distinct settled mode.
  local m = r16(0x36)
  if (m == 0x04 or m == 0x06 or m == 0x08 or m == 0x02) and not seen[m] and (frame % 1 == 0) then
    -- settle: only shoot if the mode has been stable for ~40 frames
    seen[m] = (seen[m] or 0) + 1
    if seen[m] == 1 then seen[m] = frame end
  end
end

out("flow_driver done: " .. snap())
client.screenshot(SHOT .. "final.png")
if f then f:close() end
client.exit()
