# BizHawk headless harness (SMK ground truth)

A **truly headless, scriptable, known-good SNES reference** for the recompilation.
BizHawk's `snes9x` core does HLE DSP-1 (no firmware), renders SMK perfectly, and
runs without a window via `--chromeless`, driving Lua via `--lua`.

This is what cracked the title→race menu flow that black-box-driving LakeSnes
could not: it lets us observe the *real* game's mode sequence and input timing,
then replay that exact input in our own harness.

## Setup
1. Download BizHawk (win-x64) from https://github.com/TASEmulators/BizHawk/releases
   (tested: 2.11.1). Needs the .NET 8 Desktop runtime.
2. Copy your SMK ROM to a space-free path (e.g. `smk.sfc`) — BizHawk's CLI splits
   unquoted spaces.

## Run (headless)
```
EmuHawk.exe --chromeless --lua=flow_driver.lua "C:\path\smk.sfc"
```
Output (`smk_flow.log` + `flow_*.png`) lands next to `EmuHawk.exe`.

## What it proves — the authentic flow
`Start+Y` is the universal confirm/advance button (NOT Select+Start). The title's
input gate opens only after the title fully fades in (`$0160` reaches `$0F00`,
~frame 360); presses before that are ignored.

| `$36` | Screen                               | Advance via |
|-------|--------------------------------------|-------------|
| `$04` | Title (Super Mario Kart)             | `Start+Y`   |
| `$06` | Driver select (kart machine, 1P/2P)  | `Start+Y`   |
| `$08` | 50cc Class / Mushroom·Flower·Star Cup | `Start+Y`   |
| `$02` | **Race** (Mode-7 Mario Circuit)      | —           |

`$36` = current mode (master sequencer `$81:E000`); `$32` = pending mode.

## Lua API cheatsheet (BizHawk)
- `memory.read_u8(off,"WRAM")` / `read_u16_le(off,"WRAM")` — `off` is the WRAM
  offset, i.e. `$7E0000+off`.
- `joypad.set({Start=true,Y=true}, 1)` — call every frame before `emu.frameadvance()`.
  SNES button keys: `Up Down Left Right Select Start B A X Y L R`.
- `emu.frameadvance()`, `client.screenshot(path)`, `console.log(s)`, `client.exit()`.
- `event.onmemorywrite(fn, off, "WRAM")` — fires `fn(addr,val)` on writes (used to
  trace which input/PC sets `$32`).
