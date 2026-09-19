# Super Mario Kart — Static Recompilation

Static recompilation of **Super Mario Kart** (SNES, 1992) from WDC 65C816 assembly to native C code, playable on modern hardware via SDL2.

Part of the [sp00nznet](https://github.com/sp00nznet) recompilation portfolio. This is the first SNES (65816 CPU) target in the series.

> **Contribution branch note:** `contrib/recompiler-correctness` is an experimental fork branch
> focused on generator coverage, deterministic ROM-oracle validation, and exact timed native C
> execution. It is **not yet a complete C recompile**: the default playable path still executes a
> user-supplied ROM through LakeSnes. See
> [`docs/contribution_status.md`](docs/contribution_status.md) for the exact scope, reproducible
> validation commands, current results, and remaining work.

## Status

**Playable end-to-end.** A cold boot walks the authentic flow from controller input —
title → driver select → 50cc class / cup select → a live **Mode-7 race** — with a Dear
ImGui menu bar, save-states, configurable keyboard + gamepad controls for two players, and
**lockstep netplay**. The SNES hardware (PPU Modes 0–7, SPC700 + DSP audio, DMA/HDMA, DSP-1
coprocessor) is fully emulated by the LakeSnes backend.

Two execution paths share the same backend: **real-frame mode** (the default) runs the
genuine ROM through LakeSnes's cycle-accurate frame, so the whole game plays today; the
**recompiled-shell path** (`SMK_SHELLS=1`) runs hand-written `RECOMP_PATCH` functions and is
where the static-recompilation work grows incrementally. Recompiled functions are defined
with `RECOMP_PATCH(name, snes_addr) { ... }` and auto-register in snesrecomp's dispatch table
at static-init time — adding a translated function is a one-line change, no central list.

![Title Screen](newtitle.png)

The full menu flow runs from controller input — title → driver select → class/cup select:

![Driver Select](charselect.png)

![Class / Cup Select](class.png)

The Mode-7 race renders and plays — perspective track, karts, HUD, split-screen:

![Mode-7 Race](race.png)
![Mode-7 Race 2](race2.png)

**Lockstep netplay** — two machines run the same deterministic emulation in sync (host =
Player 1, client = Player 2), starting from a shared save-state:

![Multiplayer netplay](mp.png)

A Dear ImGui menu bar (File / Graphics / Sound / Controller / Multiplayer / Help) overlays the game — see [Menu](#menu).

### What works
- **Full authentic flow from controller input** — title → driver select → 50cc class / cup
  select → Mode-7 race, all driven by the genuine ROM's state sequencer (`$81:E000`)
- **Mode-7 race** rendering + gameplay: perspective track, karts, HUD, lap timer, split-screen
- **Dear ImGui menu** overlaying the game — File / Graphics / Sound / Controller / Multiplayer / Help
- **Save-states** — File → Save/Load (and F5/F8), full machine state via LakeSnes; File → New resets
- **Configurable input** for **both players** — rebind keyboard *and* Xbox-style gamepad per button
- **Lockstep netplay** (TCP) with input-delay buffering — host/join, save-state sync on connect,
  deterministic in-sync emulation (verified by matching WRAM checksums)
- Full boot chain: reset vector → hardware init → WRAM 0x55 fill → PPU/APU/DSP-1 setup → Mode-7 tables
- Custom tile/tilemap decompressor (`$84:E09E`) — all 7 compression modes + E0+ extended counts
- Joypad/auto-joypad input with edge detection; real palettes ROM → CGRAM; per-scanline HDMA
- LakeSnes renders all PPU scanlines per frame; SDL2 window (3× scale by default), 60 fps, audio

### What's next
- **Grow the recompiled-shell path** (`SMK_SHELLS=1`) to drive gameplay — currently the shells
  render the menus but the Mode-7 race's multi-frame init/fade-in needs the real frame
  (the default real-frame path handles it). See `docs/smk_flow_re.md`.
- Netplay polish: rollback / over-the-wire desync detection; "drop straight into 2-player on connect"
- More recompiled functions (the long-term goal: replace real-frame with recompiled C)

### Recent (September 2026)

- **Two more readable binary reconstructions**: vector direction (`81F638` and
  eight branches) and direction from a packed grid-cell center (`81FD22`).
  Direct ROM tests pass 6,489,603 vector and 1,572,864 cell cases. These are
  semantic APIs, not replacements for bus-timed game execution. See
  [derivation and evidence](docs/binary_direction.md) and the
  [ongoing recovery log](docs/binary_recovery_log.md).
- **Readable algorithm recovered from ROM**: `smk_mix_word` reconstructs `$81:BB70`
  as compact C, including its unusual separate return value/state and final flags.
  All 65,536 inputs pass against actual ROM CPU execution under two initial flag
  patterns (131,072 cases). The timed generated body remains in use for game execution.
  See [derivation and exhaustive test](docs/binary_word_mix.md).
- **Coverage-first expansion**: 85 registered C entries, including 37 generated functions.
  Four new roots (`81F722`, `81FD22`, `8087D9`, `80879A`) execute 9,220 interceptions
  together and match all 140 raw snapshots of the 1,400-frame route. The 22-root `coverage`
  preset executes 21,014 interceptions (legacy default: 11,794), matching outside the
  legacy dead-stack exclusion. These counts are not whole-game completion percentages.
- Run `python tools/recomp/validate_route.py` for the strict new-root gate, or
  `python tools/recomp/validate_route.py --intercepts coverage --ignore-dead-stack`
  for the expanded combined gate. Both run a fresh ROM oracle and save local evidence.
  See [the related-project review and plan](docs/related_projects_and_coverage_plan.md).

### Earlier (August 2026)
- **Measured full-C baseline and roadmap** — the standard 1,400-frame route reaches 361 direct
  call targets. 81 C entries are registered (32 auto-generated), but only the strict 18-function
  combined set is default oracle-gated; see [`docs/recompilation_roadmap.md`](docs/recompilation_roadmap.md).
- **Generator arithmetic expansion** — added runtime-decimal-correct 8/16-bit ADC/SBC for
  existing memory modes plus NOP. This unlocks several high-frequency math routines; the
  `$81:F638` indirect-call closure is retained as an exact-timing regression rather than being
  prematurely default-enabled.
- **Auto-recompiler correctness pass** — fixed mid-function entries whose CFG branches into
  lower-address shared blocks; added M/X-width-correct register stack operations, PHK/PHD/PLD,
  TSB/TRB, and the remaining register transfers, with focused Python regression tests.
- **Two more race-path functions recompiled** — `$80:8EED` (backward/shared CFG) and `$81:BB70`
  (hot RTL bit-mixing leaf). The default 18-function timed-recomp set completed a scripted
  1,400-frame title → menus → race → driving run with 11,794 native interceptions and
  byte-identical WRAM/VRAM/CGRAM snapshots versus the ROM interpreter outside the documented
  dead stack-scratch range `$1F00-$1FFF`.
- Transition-sensitive `$80:9EB2`, `$81:81C4`, and `$85:92F9` remain generated but opt-in;
  the default set no longer claims them as oracle-safe.

### Recent (June 2026)
- **Cracked the menu→race flow** — root-caused the long-standing menu blocker to a power-on
  WRAM fill (`$2E` game-mode read as 2-player on a zero-filled RAM); fixed with a `0x55` fill
  matching hardware/snes9x. Full RE writeup in [`docs/smk_flow_re.md`](docs/smk_flow_re.md),
  cracked headlessly via a BizHawk + snes9x-core harness (`tools/bizhawk/`).
- **Real-frame mode** — runs the genuine ROM via LakeSnes's full frame so the whole game plays now.
- **ImGui menu, save-states, 2-player input, and lockstep netplay** added to snesrecomp (generic).
- **Fixed a real `s_pixel_buf` overflow** (478 vs 480 rows) that silently zeroed adjacent statics.

### Earlier
- **Auto-registered dispatch** via `RECOMP_PATCH(name, snes_addr) { ... }` — linker-priority static
  constructors, no central list. Pattern inspired by N64Recomp's `RECOMP_PATCH`.
- **Public 65816 op kit** — `<snesrecomp/cpu_ops.h>` (`op_lda_*`, `op_sta_*`, `op_rep`, …).

## Architecture

```
┌─────────────────────────────────────────────────┐
│                 smk_launcher                      │
│  ┌──────────────────────────────────────────┐    │
│  │  src/recomp/ — 81 registered C entries   │    │
│  │  smk_boot.c  — NMI, state machine, input │    │
│  │  smk_init.c  — Init, transition dispatch │    │
│  │  smk_title.c — Decompressor, PPU, menus  │    │
│  └──────────────────────────────────────────┘    │
│                       │                           │
│              bus_read8 / bus_write8               │
│                       │                           │
│  ┌──────────────────────────────────────────┐    │
│  │  snesrecomp (ext/snesrecomp/)            │    │
│  │  ┌────────────────────────────────────┐  │    │
│  │  │  LakeSnes — Cycle-accurate SNES HW │  │    │
│  │  │  Real PPU (Mode 0-7, sprites, etc) │  │    │
│  │  │  Real SPC700 + DSP audio           │  │    │
│  │  │  Real DMA (GPDMA + HDMA)           │  │    │
│  │  │  Full memory bus routing            │  │    │
│  │  └────────────────────────────────────┘  │    │
│  │  SDL2 platform (window, audio, input)    │    │
│  └──────────────────────────────────────────┘    │
└─────────────────────────────────────────────────┘
```

In the **recompiled-shell path** (`SMK_SHELLS=1`), recompiled game code acts as the CPU — it
calls `bus_read8(bank, addr)` / `bus_write8(bank, addr, val)`, which route through LakeSnes's
real memory bus to the actual PPU, APU, DMA, and cartridge hardware. In the **default
real-frame path**, LakeSnes's own CPU executes the genuine ROM each frame (`snes_runFrame`)
— identical to a cycle-accurate emulator — so the full game plays while the recompiled
functions are still being written. Either way, all PPU/APU/DMA behaviour is real.

## Controls

Default Player 1 keyboard (rebindable in the menu):

| Key | SNES Button |
|-----|-------------|
| Arrow keys | D-pad |
| Z | B |
| X | Y |
| A | A |
| S | X |
| Q | L |
| W | R |
| Enter | Start |
| Right Shift | Select |
| F5 / F8 | Save / Load state |
| Escape | Quit |

**Gamepads** (Xbox-style, via SDL_GameController) work out of the box for both
players — A→B, B→A, X→Y, Y→X, shoulders→L/R, Back→Select, Start→Start, d-pad and
left stick steer. Rebind any of it in **Controller** (P1/P2 tabs).

## Menu

A Dear ImGui menu bar (modelled on [LinksAwakening](https://github.com/sp00nznet/LinksAwakening))
overlays the game:

- **File** — New (reset/restart), Save / Load state (`smk_state.sav`; also **F5 / F8**),
  Settings ▸ (save/load `smk_config.ini`), Quit
- **Graphics** — window scale, V-Sync, texture filter, scanlines, show FPS
- **Sound** — master volume, mute
- **Controller** — rebind keyboard **and** gamepad for **Player 1 and Player 2**
- **Multiplayer** — Host / Join (IP:port), input-delay, connection status — lockstep netplay
- **Help → About** — links back to this repository

The menu auto-disables in headless/scripted runs (`SMK_HEADLESS`/`SMK_SCRIPT`).

## Building

### Prerequisites
- CMake 3.16+
- Visual Studio 2022 (MSVC)
- vcpkg (SDL2 is declared and version-locked by `vcpkg.json`)
- Python 3.10+ (for disassembler and analysis tools)

### Build

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Debug
```

Run the auto-recompiler regression tests with:

```bash
python -m unittest tools/recomp/test_autogen.py -v
```

### Run

```bash
build/Debug/smk_launcher.exe
```

When no ROM argument is supplied, the launcher searches its current/executable directory and
up to two parent directories. This allows a Release/Debug executable to be double-clicked while
the legally supplied ROM remains at the repository root. An explicit ROM path is still accepted.

The ROM file is not included — supply your own US v1.0 copy (MD5: `7f25ce5a283d902694c52fb1152fa61a`).

By default the launcher runs in **real-frame mode** — the genuine ROM via LakeSnes's
full cycle-accurate frame — so menus *and* the Mode-7 race render and play at full
speed out of the box. (The recompiled per-frame shells can't yet drive the race's
multi-frame setup; as gameplay functions are recompiled they take over.)

For recompilation development, use the recompiled-shell path:

```bash
SMK_SHELLS=1 build/Debug/smk_launcher.exe           # recompiled per-frame shells
```

Set `SMK_RECOMP_PROFILE=1` to rank direct call targets. The default report shows 40 rows;
`SMK_RECOMP_PROFILE_TOP=1000` exports the complete observed set. Targets seen under more than
one accumulator/index width include an exact `VARIANTS=...` list for safe CFG generation.

Timed native validation can replace selected ROM routines while retaining LakeSnes's real frame
loop. When validating a complete generated call closure, disable interpreter force explicitly;
otherwise registered children are intentionally routed through the untimed diagnostic fallback:

```bash
SMK_RECOMP=1 SMK_INTERP=0 SMK_RECOMP_BUSPHASE=1 SMK_RECOMP_PHASE_YIELD=1 \
SMK_RECOMP_INTERCEPTS=81F638:L build/Debug/smk_launcher.exe
```

`$81:F638` and its eight indirect JSR targets are the first exact-timing closure gated this way:
2,715 interceptions match the 1,400-frame WRAM/VRAM/CGRAM oracle at all 140 sampled frames. The
generated call-frame token preserves caller SP/PB, prevents interpreter fallback from pushing a
second return frame, and suspends the timed hook while fallback executes. The bus-phase/yield path
is still opt-in. Its generated immediate, data, implied, branch, RMW, stack, call, and return
paths now use LakeSnes-style `checkInt` micro-phases. MVN/MVP now execute one hardware-style
iteration at a time (including opcode refetch, read/write, idle/check/idle, and resumable PC),
and RTI restores P/PC/PB in LakeSnes pull/check order. These three opcodes have focused generator
coverage but do not occur in the current 37-function generated set, so they still need a ROM-level
closure gate. Generated memory modes now also place LakeSnes's dynamic DP-low, DP-index,
absolute-index page/write/width, and stack-relative idle penalties before the correct pointer or
data access. The generated `$80:946E` OAM-DMA leaf now drives LakeSnes one bus phase at a time:
1,071 interceptions match all 140 raw snapshots of the 1,400-frame oracle. Because those bus
accesses call LakeSnes directly, its DMA stall state machine and open-bus updates are preserved;
the legacy aggregate path remains approximate, and internal bus latches are not part of the
snapshot format. JSL and
`JSR (abs,X)` now also preserve their non-linear operand/stack fetch order. The newly generated
`$81:FD22` calls the `$81:F638` C closure through JSL and passes 2,684 root interceptions with
all 140 raw snapshots matching, providing a ROM gate for this cross-bank call path.
All five JMP/JML tail forms now have ordered bus-phase emission, with local decoded targets kept
inside the C CFG; indirect-tail closures still need isolated ROM-level timing gates.

## Decompressor

The custom decompressor at `$84:E09E` handles SMK's tile/tilemap compression format:

| Mode | Encoding | Description |
|------|----------|-------------|
| `$00` | Raw | Copy N bytes from stream |
| `$20` | RLE | Repeat 1 byte N times |
| `$40` | Word fill | Alternate 2 bytes for N entries |
| `$60` | Inc fill | Store incrementing byte N times |
| `$80` | Backref | Copy from earlier in buffer (abs offset + base) |
| `$A0` | Inv backref | Copy with XOR $FF (inverted) |
| `$C0` | Byte backref | Copy from buf_pos - offset (1-byte offset) |

Commands `$E0`–`$FE` use extended 10-bit counts: 1 data byte + cmd bits 0-1 as high bits.

## Project Structure

```
├── include/smk/       functions.h (smk_XXXXXX forward declarations)
├── src/
│   ├── recomp/        Recompiled game functions (smk_boot.c, smk_init.c, smk_title.c)
│   └── main/          main.c — entry point, frame loop
├── ext/snesrecomp/    snesrecomp library (LakeSnes backend + SDL2 platform):
│                        recomp_patch.h    — RECOMP_PATCH auto-registration macro
│                        cpu_ops.h         — 65816 instruction helpers (op_lda_*, op_sta_*, …)
│                        menu_overlay.cpp  — Dear ImGui menu (File/Graphics/Sound/Controller/MP/Help)
│                        mp_session.c      — generic SNES lockstep netplay (TCP)
│                        third_party/imgui — vendored Dear ImGui
├── docs/
│   ├── contribution_status.md  public status and reproducible validation for this branch
│   ├── recompilation_roadmap.md staged plan toward complete C ownership
│   └── smk_flow_re.md           reverse-engineering of the menu→race flow
└── tools/
    ├── disasm/        65816 disassembler (M/X flag tracking, all addressing modes)
    ├── bizhawk/       headless BizHawk + snes9x-core ground-truth harness
    └── lakesnes_ref.c native LakeSnes runner (scripted input, snapshot/BMP dumps)
```

## ROM Details

| Field | Value |
|-------|-------|
| Title | SUPER MARIO KART |
| System | Super Nintendo (SNES) |
| CPU | WDC 65C816 @ 3.58 MHz |
| Coprocessor | DSP-1 (math) + SPC700 (audio) |
| Mapping | HiROM FastROM |
| Size | 512 KB (8 × 64 KB banks, C0–C7) |
| SRAM | 2 KB |
| Region | USA |
| CRC32 | CD80DB86 |

## Key References

- [Yoshifanatic1/Super-Mario-Kart-Disassembly](https://github.com/Yoshifanatic1/Super-Mario-Kart-Disassembly) — Full 65816 + SPC700 disassembly (Asar)
- [jvipond/super_mario_kart_disassembly](https://github.com/jvipond/super_mario_kart_disassembly) — Trace-based disassembly with Python tooling
- [jvipond/super_mario_kart_recompilation](https://github.com/jvipond/super_mario_kart_recompilation) — Prior LLVM-based recomp attempt
- [MrL314/smk-spc700-disassembly](https://github.com/MrL314/smk-spc700-disassembly) — SPC700 audio driver disassembly
- [LakeSnes](https://github.com/elzo-d/LakeSnes) — Cycle-accurate SNES emulator in C (hardware backend)

## License

This project contains no Nintendo copyrighted material. The ROM file is not included and must be legally obtained by the user.
