# SMK Game-Flow Reverse Engineering (intro → menus → race)

_Goal: recreate SMK's authentic screen flow, not the recomp's simplified one.
Method: drive the native ROM in LakeSnes (`tools/lakesnes_ref`, now with input
scripting + WRAM tracing) and trace the live state machine._

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

## The open blocker — title → menu input trigger
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
