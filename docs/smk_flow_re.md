# SMK Game-Flow Reverse Engineering (intro → menus → race)

_Goal: recreate SMK's authentic screen flow, not the recomp's simplified one.
Method: drive the native ROM in LakeSnes (`tools/lakesnes_ref`, now with input
scripting + WRAM tracing) and trace the live state machine._

## ⭐ CORRECTION (2026-06-02) — menu entry WORKS; `$0E68` was a red herring

Confirmed against **snes9x** (independent emu, HLE DSP-1, no firmware): real SMK
enters the menu with **SELECT+START** at the title — plain Start does nothing
(matches the Select-gated handler at `$80:8559`). Then **reproduced headlessly in
our own LakeSnes harness**:

- A single Start (or single Select+Start) does nothing, but pressing **Select+
  Start a few times across the title** (e.g. bursts at f400/460/520/600/700)
  enters the menu: `$32=$0006` → `$36=$0006` (the **1-player driver select** —
  kart-machine with Mario/Peach/Bowser/Luigi portraits, "1P" badge).
- **`$0E68` stayed `0000` the whole time** — it was never the gate. The earlier
  "gate never opens" conclusion (above, kept for history) was WRONG; the real
  blocker was just input timing/sequence. LakeSnes has no bug here.
- So the recomp/harness CAN drive the authentic flow; no external emu needed.

### Driver select (`$36=$06`) — fully functional; confirm not yet found
- The screen IS working: marquee scrolls **"CHOOSE YOUR DRIVER"**, and the
  **cursor responds to input** — `$81` (cursor index) moves 0→1→2→3→4 via
  `$85:8618` (inc) / `$85:8624` (dec), driven by a processed menu-input byte
  `$80` (action codes, set around `$85:867B`/`$8687`/`$8272`). So input reaches
  the menu logic; the "unresponsive" read earlier was just the small cursor delta.
- **driver→class→cup are INTERNAL sub-states of mode `$06`** — bank `$85` has only
  ONE `STA $0032` (the title→`$06` entry at `$85:85F3`), so advancing between
  driver/class/cup does NOT change `$36`/`$32`; it's a sub-state inside `$06`,
  changing `$36`/`$32` only at the end (→ race `$02`).
- **CONFIRM input found = B (or Start).** Per-button WRAM dump-diff at driver
  select: LEFT/RIGHT = scroll (move cursor `$007E`, ~5-6 byte changes);
  **B and Start each cause ~47 WRAM changes** (a select/preview: Mario dons his
  racing helmet, sprite block `$0370+`, anim counter `$0070` cycles `$64→$9C`
  via `$85:815D`/`$88CE`); A/X/Y/L/R/Select/Up/Down do nothing. So input the menu
  reacts to is **LEFT/RIGHT (scroll) + B/Start (select)** — matches the disasm
  (`$85:8640+` translates pad → `$80` action codes; action `$10` → `STZ $7D;
  JSR $8865` is the select).
- **Open:** the select **doesn't complete the advance** to cup/class. B/Start
  starts the helmet/`$0070` animation but it **reverts** (Mario back to normal by
  ~f2200) and `$0150` (RaceCup) is never set across every pattern tried (single
  B, double B, RIGHT→B→B, Start×2). So driver-select→cup needs a specific
  completion sequence (cursor must land on a driver with the marquee showing its
  NAME, not the "CHOOSE YOUR DRIVER" prompt? a hold? a 2nd confirm after the kart
  drives in?). NEXT: decode the `$80==$10` select path (`$85:8865`) and what gates
  the cup advance; or trace what writes `$0150`/the sub-state when a driver is
  truly locked in. Screenshot of the select (helmet) is the milestone.

Tooling: `lakesnes_ref` `SMK_REF_SCRIPT` (input) + `SMK_WATCH_WRAM` (writes w/ PC).
Entry recipe: SELECT+START bursts at f400/460/520/600/700 → driver select.

## Authentic attract flow (native, no input) — mapped
| frames | `$36` | screen |
|--------|-------|--------|
| 1–126  | `00`  | black boot |
| 127–167| `1A`  | black init |
| 168–208| `00`  | black |
| 209–1890| `04` | **title** (logo; idle timer `$1040` counts to `$0642`) |
| 1891–1972| `00`| fade to demo |
| 1973+  | `02`  | **attract demo race** (mode 7, now renders correctly) |
| (loop) |       | START during demo → back to title `$04` |

There is **no separate Nintendo logo** screen (the ©1992 Nintendo is on the
title). The recomp **shortcuts** this: it boots straight to `$36=04`, skipping
`$00→$1A→$00`.

## Master mode sequencer — `$81:E000` (reverse-engineered)
The high-level screen flow is driven here (this is the "real init" the Mode-7
fix already interprets). Key mechanism (via `SMK_WATCH_WRAM` traces + disasm):

- **`$32`** = the *pending* next mode; **`$36`** = current mode.
- A mode change fires only when **`$32 != 0` AND `$0160 == $8000`** (`$0160`/`$0161`
  = ScreenDisplayRegister; `$8000` means the screen has finished fading OUT).
  Dispatcher at `$81:E060`–`$E0A0`:
  ```
  LDA $32; BEQ skip
  LDA $0160; CMP #$8000; BNE skip      ; wait for fade-out
  ... STZ $420B/$420C ...
  LDX $32; JSR ($E049,X)               ; run the per-mode INIT handler
  LDA $32; STA $36                     ; $36 <- $32  (enter the new mode)
  STZ $0E68                            ; (clears the title input gate)
  ```
- **Per-mode init table at `$81:E049`** (indexed by `$32`):
  `$00→$E0AC  $02→$E126  $04→$E12E  $06→$E2F3  $08→$E13E  $0A→$E22B`
  `$0C→$E388  $0E→$E390  $10→$E398  $12→$E258  $14→$E34F  $16→$E0AC  $18→$E136`
- Per-frame dispatch (separate): main `$80:8197`, NMI `$80:81BF`, both indexed by
  `$36`. State `$04` main = `$80:80BA` (title) → `JSR $80:853D` (input handler).

**So: to move to any screen, code sets `$32 = <mode>` and starts a fade-out;
when the fade completes (`$0160=$8000`) the sequencer enters it.**

## Title → menu trigger — FOUND (the input action)
Disassembling the title input handler `$80:853D` (called by state-`$04` main
`$80:80BA`): when input is enabled it reads controller `$22` (`$20,X`) and on
**START (`BIT #$1000`) branches to `$85B9: LDA #$0014` → `$85D8: STA $32`** — i.e.
**START sets `$32 = $14` (mode select)** and initiates a fade-out (`$48=$8F00`,
`$015E=$60`). The demo-timeout path shares the same `STA $32` with `A=$02`. So:

> **Title + START → `$32=$14` + fade → sequencer enters mode `$14` (mode select).**

Mode `$14` init = `$E34F` (from the `$E049` table). This matches the recomp's
`$14`=mode-select — so the recomp's *target* states are right; what's fake is the
*navigation/transitions* between them.

Forcing it (poke `$32=$14`+`$48=$8F00`+`$015E=$60` at the title) makes the
sequencer fire but it lands at `$36=$00` with a garbage screen — the mode-`$14`
init needs the full title-handler setup, so a blind poke can't substitute.

## The open blocker — the title input GATE `$0E68`
The title input handler `$80:853D` reads the controller (`$20,X`) only when the
gate **`$0E68 != 0`**. But:
- `$0E68` is written **exactly once** in the whole ROM — a single `STZ` (clear) at
  `$81:E0BD`/`$E0C0`. No static `STA` setter (any addressing mode) exists.
- In the entire attract it stays `0000`, so the title never reads input — START at
  the title does nothing (confirmed in BOTH native and recomp).
- Input plumbing is fine: a START press reaches the controller shadow (`$0020/$0024`
  = `$1000`) with ~2-frame latency, and START *during the demo* correctly returns
  to the title. So only the title's own input is gated off.
- Poking `$0E68` non-zero + pressing START did **not** transition (likely the game
  re-clears it before the read, or the gate decode is incomplete).

### Round 2 findings (deeper, still unsolved)
- The input loop is **Select-gated**: `$8559 BIT #$2000` (Select), `$855C BEQ`
  skips unless Select is held; only then are B/**Start**/A checked. So even with
  the gate open, the menu entry wants a **Select+Start**-style combo, not plain
  START. (Or `$20/$22` use a non-standard bit order — unverified.)
- PC-tracing (`SMK_PC_TRACE`) the handler: `$80854F`/`$808552` (the `$0E68` read +
  BEQ) run every title frame and **always branch to `$80856E` (gate closed)**;
  `$808554` (the input loop) is never reached. The loaded `$0E68` value (`A` at
  `$808552`) is **always `$0000`**.
- Poking `$0E68=1` is inconsistent: the value persists to end-of-frame (snapshot
  shows `0001`), yet the handler still reads `0` at `$808552`, and poking it
  alone causes no transition. The gate effectively cannot be forced from outside.

### Next angles (unsolved)
1. Find what *sets* `$0E68` — likely an indexed/computed store, or it's set on a
   path not exercised by plain attract. Trace `$0E68` reads/writes across a full
   attract cycle incl. the demo→title return.
2. Reconsider whether `$36=$04` is the *interactive* title or only the attract
   title; the interactive "PUSH START" title may be a different `$32` mode whose
   init enables input.
3. The Yoshifanatic1 disassembly is **incomplete** (ROM map is a 3 KB include
   stub); try jvipond's trace-based disassembly for the title/menu routines, or
   trace `$32`/`$0160` writers (`$80:B1B3` drives `$0160`) to find the screen-
   change callers.

## Tooling (committed)
- `tools/lakesnes_ref`: native ground-truth driver — `SMK_REF_SCRIPT="f:BTN,..."`
  (input), `SMK_REF_EVERY=N` (periodic snapshot+BMP), `SMK_REF_POKE="off:val:frame"`
  (force a WRAM word), `$36/$32` state trace.
- `SMK_WATCH_WRAM=<hex off>`: log every write to a WRAM offset with CPU PC (native
  + recomp).
