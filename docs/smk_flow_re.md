# SMK Game-Flow Reverse Engineering (intro → menus → race)

_Goal: recreate SMK's authentic screen flow, not the recomp's simplified one.
Method: drive the native ROM in LakeSnes (`tools/lakesnes_ref`, now with input
scripting + WRAM tracing) and trace the live state machine._

## ⭐⭐⭐ CRACKED — full flow driven headlessly (2026-06-03)

Solved with a **headless BizHawk + snes9x core** harness (`tools/bizhawk/`,
`--chromeless --lua`, HLE DSP-1, no firmware). Tapping a single button walks a
cold boot all the way into a live Mode-7 race — observed AND reproduced:

| `$36` | Screen | Advance | Reference |
|-------|--------|---------|-----------|
| `$04` | Title (Super Mario Kart) | `Start+Y` (after fade-in) | `docs/flow/flow_04_title.png` |
| `$06` | Driver select (kart machine, 1P/2P) | `Start+Y` | `docs/flow/flow_06_driver.png` |
| `$08` | 50cc Class / Mushroom·Flower·Star Cup | `Start+Y` | `docs/flow/flow_08_class_cup.png` |
| `$02` | **Race** (Mode-7 Mario Circuit, "ROUND 1") | — | `docs/flow/flow_02_race.png` |

**Two facts that unblocked everything (both prior assumptions were wrong):**
1. **The confirm button is `Start+Y`, not `Select+Start`.** snes9x's default keyboard
   maps **Space → Y**, so the user's "start + space" was always **Start+Y**. Every
   Select+Start attempt (LakeSnes and BizHawk) correctly *reached* the game
   (`$0020=$3000`) but the title simply doesn't act on it.
2. **The title has an input gate that opens only after it fully fades in**
   (`$0160` reaches `$0F00`, ~frame 360). Presses during the fade are ignored —
   which is why isolated early presses failed while later/repeated taps worked.

So `$06`, `$08`, `$02` are **separate modes** (the master sequencer at `$81:E000`
sets `$32`=pending then copies `$32`→`$36`), NOT sub-states of `$06`. The
`# players → GP/TimeTrial` choices are defaulted/auto-accepted by the rapid
`Start+Y` taps. The empirical `$32` sequence per run: `$04→$06→$08→$02`.

Recipe (see `tools/bizhawk/flow_driver.lua`): tap `Start+Y` in ~6-frame pulses
every ~70 frames once `frame ≥ 360`. Reaches `$36=$02` (race) by ~frame 1045–1255.

> NEXT for the recomp: replay this exact `Start+Y` input timing in our LakeSnes
> harness / recompiled build and diff against the snes9x ground truth.

## ⭐ LakeSnes replay + divergence pinned to the `$06` confirm gate (2026-06-03)

Replayed the snes9x Start+Y/Start timing in our own backend via `tools/lakesnes_ref`
(pure LakeSnes, `SMK_REF_SCRIPT="frame:BTN,..."`, `SMK_WATCH_WRAM=<off>`). Result:

**LakeSnes reproduces the flow up to `$06`, then stalls.** It matches snes9x exactly
through driver-select entry, then refuses to advance to `$08`:

| Stage | snes9x (BizHawk) | LakeSnes (`lakesnes_ref`) |
|-------|------------------|---------------------------|
| Title `$04` | f209 | f209 (identical) |
| `Start+Y` → `$06` | $32←06 then $36=06 | $32←06 @PC `85:85F6`, $36=06 @f599 ✅ |
| **`Start` → `$08`** | $32←08, $36=08 @f896 ✅ | **never — `$32` never written again** ❌ |

The confirm button is **`START` alone** (Start+Y worked only because of the Start
bit; `Y`-alone advances neither).

**Everything "easy" is ruled out — the input path is fully healthy in LakeSnes:**
- Input reaches the `$06` handler: cursor `$7E` steps `06→0C→12→18` on LEFT/RIGHT
  (handled at PC `85:9478`).
- The **START edge is correctly computed**: `$0029=$10` fires 10× during the Start
  pulses at `$06` (edge routine PC `80:8451`; current-held `$0021=$10` @PC `80:844A`).
- Sub-state `$2C`/`$2E` are **identical** to snes9x (`00`/`00`; written @PC
  `85:85C2`/`85:85B0`).
- The screen is **fully faded in** at `$06` (brightness 15 by ~f720, mode 0).

⇒ The `$06` driver-select handler **sees the START edge but never fires the
`$32←$08` advance**, where snes9x does with identical input. This is a real
**LakeSnes-vs-snes9x emulation-accuracy divergence inside the driver-select confirm
gate** — not input delivery, edge detection, sub-state, or fade timing.

## ⭐⭐⭐ SOLVED — power-on WRAM fill ($2E game-mode). FIX SHIPPED (2026-06-03)

Cracked with a **trace-diff harness** added to LakeSnes: `cpu.c` gained a frame-gated
range trace (globals `g_cpuTraceOn/Lo/Hi`), driven by `tools/lakesnes_ref` via
`SMK_TRACE_RANGE="lo-hi"` + `SMK_TRACE_FRAMES="n,..."`. Diffing the `$06` handler on a
Start-press frame vs a no-press frame (isolating just the input edge) walked straight
to the gate.

**The confirm chain** (`$06` per-frame handler `$85:90B1`, all 16-bit M/X):
- `$85:9487` `LDA $6A / ORA $6C / AND #$9000 / BNE $949A` — confirm = **B or START**
  edge. (The button is START; Y is irrelevant. `$6A`/`$6C` are the joy1/joy2 edges.)
- `$949A` → first press **locks** the cursor's player (`$70,x = 1`, SFX `$002E`);
  second press (`$70,x==1`) → `$952C`, sets `$70,x = 2` ("player done").
- `$85:965B` is the **advance gate**: `LDA $70 / CMP #2 / BNE rts` **and**
  `LDA $72 / CMP #2 / BNE rts` — requires **BOTH** 1P (`$70`) and 2P (`$72`) == 2,
  then a 64-frame settle (`$96`→`$40`) before advancing to `$08`.

**Why snes9x advanced but LakeSnes didn't:** in snes9x `$72=2` (2P auto-done) at `$06`
entry, so 1P-only confirm satisfies the gate. In LakeSnes `$72` stays 0 → waits forever
for a player-2 confirm that controller-1 never gives. That traces to **`$2E` (game
mode)**: clamp at `$81:E46A` is `LDA $2E / AND #$FFFE / CMP #5 / BCC keep / LDA #2 /
STA $2E` — i.e. **garbage → 2** (1-player, 2P auto-done) but a clean **0 → 0**
(2-player). snes9x/hardware power-on WRAM is non-zero (`0x55` fill → every word
`$5555` → `$2E`=2); LakeSnes did `memset(ram, 0, …)` → `$2E`=0.

(The earlier "`$2C`/`$2E` identical 00/00" note was a red herring — that read the byte
written by the entry path, not the boot-time game-mode that the `$81:E46A` clamp
resolves from uninitialized RAM.)

**FIX (`ext/snesrecomp/ext/LakeSnes/snes/snes.c`):** power-on WRAM fill `0x00 → 0x55`,
matching snes9x/hardware. Verified: with **pure controller input** (Start+Y to enter,
Start to confirm) and **no pokes/hacks**, `tools/lakesnes_ref` now walks
`$04 → $06 (f599) → $08 (f826) → $02 race (f1045)` and renders the Mode-7 track
(`tools/ref_flow2_f001200.png`). This is the same divergence that blocked the recomp's
menu→race flow all along.

### Live recomp launcher — genuine flow integrated (2026-06-03)

The full launcher now runs the genuine flow too. `snes_loadRom` → `snes_reset(hard)`
applies the `0x55` fill, and **force-interpret** (the default; `SMK_INTERP=1`, NOT
`SMK_INTERP=0` which prefers the recompiled placeholder handlers) runs the genuine
`$81:E000` sequencer. With pure scripted controller input (`SMK_HEADLESS=1
SMK_SCRIPT=...`, Start+Y to enter, START to confirm) and **no `SMK_FORCE_FROM_STATE`**,
`smk_launcher` walks:

```
f1   $36=04 (title, $32=0 — genuine; no $14 detour)
f481 $32=06 -> f482 $36=06 (driver select, renders: docs/flow/recomp_driver_select.png)
f724 $32=08 -> f725 $36=08 (class/cup)
f811 $32=02 -> f812 $36=02 (RACE state)
```

So the **menu→race navigation is fully integrated** into the live recomp, and the menu
screens render. (`SMK_INTERP=0`'s simplified `$04→$14→$06` path is now obsolete for this
flow.)

**Remaining (separate, pre-existing) — race rendering, localized 2026-06-03:** at
`$36=$02` the launcher's PPU stays `forcedBlank=1 brightness=0 mode=1`, where
`lakesnes_ref` (full `snes_runFrame`) renders Mode-7 fine. Diagnosed:
- The race's **M7 raster init DID run** — `$A4` is set to 6 at PC `81:FB83`
  (`$81:F7F1→$FB7E`), so the per-scanline M7 HDMA pointers are configured. The gap is
  **downstream of init**, in the recomp's manual frame model:
  1. **Per-scanline HDMA** isn't reproduced — the race sets `BGMODE=7` via per-scanline
     HDMA on `$2105`, but the launcher's *untimed* end_frame HDMA leaves `mode=1`.
  2. **Brightness fade-in** never happens — the race stays force-blanked (`$2100` bit7,
     brightness 0); the recompiled NMI brightness shell `smk_80B181` doesn't drive the
     race fade.
- Root: the launcher drives frames manually (`smk_808000` NMI + `smk_808056` main +
  manual PPU render + untimed HDMA in `end_frame`), which doesn't replicate the
  per-scanline HDMA + brightness behavior that real `snes_runFrame` does. The menus
  ($04/$06/$08, mode 0/1, no per-scanline tricks) render fine; the Mode-7 race needs the
  real per-scanline frame.
- Fix options: (a) make `end_frame` HDMA per-scanline for the race (proper recomp work);
  (b) in *full* force-interpret mode, drive the frame via `snes_runFrame` like
  `lakesnes_ref` (gives a fully-playable game now, but bypasses the recompiled shells —
  a stopgap, not the recomp end-goal). The old `SMK_FORCE_FROM_STATE=08` hybrid rendered
  it via end_frame-HDMA, so a path exists.

**Deeper trace (2026-06-03b) — the real blocker is the fade-in, not HDMA:** per-scanline
HDMA is ALREADY implemented in `snesrecomp_end_frame` (`dma_hdmaRunLine` per line before
`ppu_runLine`). The actual cause:
- `$2100` (INIDISP) is written **only on change** (≈15 writes / 900 frames). The
  recompiled brightness handler `smk_80B181` (`smk_boot.c`) writes `$2100 = $0161` (high
  byte of the fade value `$0160`). At the race `$2100` stays `$80` ⇒ `$0160=$8000`, i.e.
  **faded-OUT / force-blank**. The race's **fade-IN trigger (`$48>0`) never fires**, so it
  never climbs back to `$0F00`.
- The screen-reset routine `$84:F388` (runs at race setup: `STZ $420C` disables HDMA,
  `$2100=$80` force-blank, clears windows/color-math/OAM) leaves HDMA off + force-blank;
  on hardware the race then re-enables HDMA and fades in over several frames.
- Both the fade-in and HDMA re-enable are part of the **multi-frame race-init / per-frame
  loop** that the recomp's manual shells (`smk_808000`+`smk_808056`, `smk_81E067` dropping
  vblank waits) don't complete. `lakesnes_ref` (full `snes_runFrame`) completes it.
- ⇒ Proper fix is frame-model work: let the race's init+fade-in+HDMA-enable sequence run
  to completion (e.g. interpret the genuine brightness/transition path through real frames,
  not the collapsed shells). Not a small targeted patch — scoped for a dedicated pass.

### ✅ RESOLVED — real-frame mode (2026-06-04)

Rather than re-architect the shells to complete the multi-frame race init, the
launcher gained a **real-frame mode** (`SMK_REALFRAME=1`): it bypasses the recomp
boot chain + per-frame shells and runs the genuine ROM via LakeSnes's full
cycle-accurate frame — `snesrecomp_realframe_begin()/end()` wrapping
`snes_runFrame()`, exactly how `tools/lakesnes_ref` renders. Verified end-to-end:
the launcher walks `$04→$06→$08→$02` from controller input and **renders the Mode-7
race** (`docs/flow/recomp_race_realframe.png` — perspective track, karts, HUD), where the shell path
stayed `mode=1`/forced-blank. The brightness handler `smk_80B181` was confirmed a
faithful port of the genuine `$80:B181` (fade-in/out logic identical line-for-line),
so the blank race was purely the shells dropping the init's vblank waits — which
real-frame avoids by running real continuous execution.

This is the practical fix for a playable game now (gameplay isn't recompiled yet, so
real continuous execution is appropriate). The recompiled **shell path remains the
default** for incremental recompilation work; as gameplay functions get recompiled,
the shells improve and real-frame becomes unnecessary. `SMK_REALFRAME=1` to play.

---
### Superseded hypothesis (kept for history)

_Earlier (2026-06-02) I believed the menu screens were all **sub-states of `$36=$06`**
and that input handling was the blocker. Both were wrong — see above. The real
issue was the button (Start+Y) + the title fade-in gate. Original notes follow._

Real SMK 1-player sequence (observed in snes9x):
```
title --(START+Y)--> driver select --> 50cc class / cup select --> RACE
   (# players / GP-vs-TimeTrial accepted as defaults via repeated Start+Y)
```

**Input-timing investigation (2026-06-02) — RULED OUT as the cause:**
- `snes_runFrame` reads the controller via `snes_doAutoJoypad` at vblank start
  (snes.c:188-191) then fires NMI — a normal ~1-2 frame input latency, identical
  to real hardware / snes9x. Not a bug.
- `snes_setButtonState` (snes_other.c:137) sets `input1->currentState`; auto-joypad
  latches+reads it. Input reaches the game correctly — **proven**: at `$06` the
  cursor scrolls on LEFT/RIGHT and B "selects" (Mario's helmet). So input is NOT
  dropped.
- Yet NO input (B / Start / Select+Start / multi-press sweeps of each) advances
  the `$06` sub-state to the next screen. ⇒ The blocker is the **menu state
  machine**, not input timing. Either a real LakeSnes-vs-snes9x accuracy
  divergence on this menu, or a specific input sequence/condition not yet found.
- NEXT (different tack): trace the `$06` sub-state variable + the `$85:90B1`
  dispatch to find the advance condition; OR capture the snes9x menu run (sub-
  state values + which input advances each screen) and diff against LakeSnes.

**Sub-state trace (2026-06-02):** the `$06` dispatch (`$85:92F9`/`$935B`) gates on
`$48`(fade)/`$0161`(brightness)/`$0198`/`$0E66`/`$2C`/`$2E`. `$2C`/`$2E` are set
by input in the title→`$06` entry code (`$85:85C0+`: STZ/`#$02`/`#$04` → `$2C`;
`#$02`/`#$04` → `$2E`) and checked (`$2C==4 && $2E==2`). BUT at the reached
screen `$2C=$2E=$00`, and **poking `$2C`/`$2E` to (4,2)/(2,2)/(0,4)/(2,4) does NOT
change the screen** — it stays the same kart-machine. So `$2C`/`$2E` are
sub-selections (player count / mode), not the screen selector. The menu is likely
**one machine screen whose marquee prompt + portraits cycle** through
#players→mode→class→character (not distinct full screens), which is why VRAM only
showed ONE transition (title→machine) and inputs cause only small (~220-word)
deltas. The navigation/advance condition remains uncracked; the finicky
multi-press entry compounds it. Best next move: **observe the snes9x run's marquee
text + `$2C`/`$2E`/`$0150` progression** to learn the exact per-prompt input,
since black-box driving in LakeSnes keeps landing in ambiguous states.

**Replication status in LakeSnes (our harness):**
- Entry works but is finicky: a single START+SELECT does nothing; needs several
  bursts across the title (f400-700) to enter `$06`. (snes9x enters on one press
  — a LakeSnes input-timing/title-state divergence.)
- At `$06` the screen is functional (cursor scrolls via LEFT/RIGHT, B selects —
  helmet sticks) but the **sub-state never advances** to the next screen (mode/
  class/etc.); `$0150` (cup) is never reached.
- ⇒ The bug to chase: why LakeSnes' `$06` sub-state machine doesn't advance like
  snes9x (input timing in `lakesnes_ref`, or a frame-model issue, or the `$06`
  sub-state dispatch in `$85:90B1`). The `$06` screen is likely **# of players**
  (first sub-state), not character-select.

---

## CORRECTION (2026-06-02) — menu entry WORKS; `$0E68` was a red herring

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
  drives in?).

### Update — B selection STICKS but no cup advance (deep wall)
- B genuinely selects: post-B the screen changes ~220 VRAM words and **stays**
  (Mario keeps his helmet from f1600→f3600 — it does NOT revert; earlier "revert"
  read was wrong). So the select is locked in.
- BUT `$0150` (RaceCup) is **never written** across every advance pattern tried
  (B; B+B delayed; B+Start; B+A; B+Y; RIGHT+B). The screen stays driver-select
  (marquee still "CHOOSE YOUR DRIVER"); it does not transition to cup/class.
- Select handler `$85:8865` is OAM/anim setup → `JMP ($83B1,X)` action table
  (action 0/1→`$8893`, 2→`$88DB`, 3→`$8939`, 4→`$8986`, 5/6→`$89E2`); `$8893`
  ends `LDA $85; AND #$01; JMP $89E7` (more anim). The cup advance is gated
  somewhere deeper. `$0150` writers live in `$C0:8575/$C0:9CC0/$C1:C2D5/$C1:E0ED/
  $C4:FAD1+` (the cup-select code).

### DECISIVE next check (fork)
Is this a harness bug or a flow misunderstanding? **Verify in snes9x:** after
Select+Start → driver select, can a real run pick a driver and advance to
cup/class → race? If snes9x advances, LakeSnes has a menu-state bug to find; if it
also can't, then `$06` (entered via the `$85:85F1` Select+Start path) is a
special/partial mode and the canonical game-start path is different (plain Start
"does nothing" in both emus, which is itself suspicious). Either answer redirects
the work cleanly.

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
