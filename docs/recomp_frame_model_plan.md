# Plan — Replacing Real-Frame Mode with Static Recompilation

_Real-frame mode (`snes_runFrame`, the default) plays the whole game but is pure
emulation, not recompilation. This is the plan to replace it with a path where
recompiled C drives the game — including the Mode-7 race — and the interpreter
fallback shrinks toward zero._

## 1. Where we are

Two execution paths share the LakeSnes backend:

| Path | How it runs | Status |
|------|-------------|--------|
| **Real-frame** (default) | `snes_runFrame()` — LakeSnes's own CPU runs the genuine ROM, cycle-accurate | Plays everything, incl. the race. **Pure emulation.** |
| **Shell** (`SMK_SHELLS=1`) | Recompiled frame shells (`smk_808000` NMI + `smk_808056` main) dispatch to recompiled-or-interpreted handlers | Renders menus; **can't drive the Mode-7 race.** The recomp path. |

The goal is to make a recompiled path that does what real-frame does, then delete
real-frame.

## 2. Why the shell path can't replace real-frame yet (root cause)

The shell path uses an **untimed, manually-driven frame model** (`recomp_interp.c`
header is explicit about this):

- snesrecomp drives the PPU and NMI **manually** — one NMI pass + one main pass per
  frame (`snesrecomp_end_frame` renders the scanlines; the shells fire the NMI).
- The interpreter runs un-recompiled code with **untimed** memory handlers (no
  master-cycle advance) to avoid double-rendering.
- This imposes an **external** frame cadence (one shell call = one display frame).

But the genuine ROM has its **own** cadence baked into the logic:

- Vblank-synchronised spin-waits embedded mid-routine (on `$44` = NMI flag, `$4212`
  = HVBJOY). The interp band-aids these (`apu_catchup_if_port`, `ppu_status_advance`)
  by faking the polled flag so the spin exits.
- **Multi-frame sequences** that span several display frames inside one logical
  operation — e.g. the race init (`$36=$02`): force-blank → load track graphics over
  several vblanks → enable per-scanline HDMA (BGMODE=7) → fade in (`$48`). The shell
  calls this transition once and the band-aids can't supply real frame advancement
  (NMI firing + PPU render + display) **between** its steps, so the load/fade never
  completes → the race stays `forcedBlank`/`mode=1`. (Diagnosed in
  [`smk_flow_re.md`](smk_flow_re.md).)

Real-frame works because LakeSnes advances the master clock per instruction, steps
PPU/APU/DMA from it, and fires NMI/IRQ at the right cycle — **the genuine code's
cadence drives the frames naturally**, no band-aids.

## 3. Key insight — real-frame and "proper recomp" are closer than they look

Both want the **same timed frame loop**. The only difference is *who executes the
code*: real-frame uses the LakeSnes CPU for everything; a proper recomp uses
recompiled C where available and the CPU (interp) elsewhere. So:

> **"Replacing real-frame" = keep the timed frame loop, but intercept recompiled
> functions inside it** — at a recompiled function's entry address, run the C
> function instead of interpreting it.

This reframes a scary "rewrite the execution core" into a tractable, incremental
change: an emulator backbone for correctness (timing, interrupts, un-recompiled
code, the SPC700, DSP-1), with recompiled C functions plugged in one at a time.

## 4. Target model: timed loop + recompiled-function interception

1. The frame loop is `snes_runFrame`-style: the master clock advances as code runs;
   PPU/APU/DMA step from it; NMI/IRQ fire from it. (Correct multi-frame behaviour
   for free — the race just works, as it already does in real-frame.)
2. A **dispatch hook** at the CPU's instruction-fetch: when `PB:PC` equals a
   registered recompiled function's entry, transfer to the C function instead of
   interpreting; on return, continue the timed loop.
3. **Cycle accounting**: most SMK routines only matter for their *bus effects*, not
   internal sub-frame timing — run the recompiled body untimed and resync at the
   function boundary (advance the clock past the call, or simply let the next timed
   instruction continue). Genuinely timing-sensitive code (raster splits, exact NMI
   work) **stays interpreted** until it's worth recompiling carefully.
4. Spin-waits resolve naturally (the NMI runs and sets `$44`); the band-aids become
   unnecessary and are deleted.

Result: the recomp path is behaviourally identical to real-frame, executed through
recompiled C where it exists. Real-frame becomes redundant.

## 5. Strategic fork — hand-written vs automated recompiler

- **Option A — extend what we have.** Keep hand-written `RECOMP_PATCH` + the LakeSnes
  interp; build the timed loop + interception hook; recompile functions incrementally.
  Lowest re-platform cost; reuses everything; slow to reach "fully recompiled".
- **Option B — adopt/port an automated recompiler.** `mstan/snesrecomp` is a mature
  AOT 65816→C recompiler (M/X-flag inference, dispatch/tail-call autorouters,
  cooperative-scheduler HLE; SMW & Mega Man X believed fully playable). Pointing it at
  SMK could auto-translate most of the ROM. But it has its **own runtime** (no
  LakeSnes) — adopting it is a re-platform, and its model would need wiring to our
  SDL/menu/netplay shell.
- **Recommendation:** do **Option A's timed loop + interception first** (it's the
  enabler and makes the recomp path play the full game *now*, validated against
  real-frame). In parallel, **evaluate Option B** as the engine for bulk translation
  — study its decoder/M-X-inference (which already solves the flag-tracking our naive
  `disasm65816.py` lacks) and decide whether to port its codegen onto our LakeSnes
  backend, or migrate. Don't block the frame-model fix on that decision.

## 6. Phased plan

**Phase 1 — Timed frame loop + recompiled-function interception (the enabler).**
- Add a play path that runs `snes_runFrame`-style timing with a CPU instruction-fetch
  hook that dispatches registered recompiled functions. Un-recompiled code interprets
  *with timing on* (the existing interp, minus the untimed-handler swap and the
  spin-wait band-aids).
- Acceptance: this path reaches **and renders** the race from controller input, with
  output matching real-frame.

**Phase 2 — Correctness gate (diff harness).**
- Reuse `SMK_SNAPSHOT_PREFIX` + `tools/diff_snapshots.py` and the netplay WRAM
  checksum: run the recomp path and `lakesnes_ref` (`snes_runFrame`) **lockstep** on
  identical scripted input, diffing WRAM/VRAM/CGRAM each frame. `snes_runFrame` is the
  **oracle**. Green across the full menu→race flow = the recomp path is faithful.

**Phase 3 — Shrink the interp (the actual recompilation work).**
- With Phase 1 green, every un-recompiled function runs correctly via the timed
  interp. Recompile the **hot / gameplay** functions (race main `$80:8067`, kart/AI
  update, Mode-7 camera + DSP-1 callers, OAM build) as `RECOMP_PATCH`, each gated by
  the Phase-2 diff harness. (Or feed them through the Option-B recompiler.)
- Track coverage: % of executed instructions running as recompiled C vs interpreted.

**Phase 4 — Make recomp the default; demote real-frame.**
- When the recomp path renders the full game and the diff harness is green, flip the
  default and remove the `SMK_REALFRAME`/`SMK_SHELLS` split. Keep `snes_runFrame` only
  as the **validation oracle** (a test dependency), not a play path.

## 7. Technical hurdles to budget for

- **Interrupt dispatch into recompiled code.** With timing on, NMI/IRQ fire
  mid-stream. While the interp handles this on the LakeSnes CPU, *recompiled* functions
  must be re-enterable at safe points (function entry is the natural boundary, since we
  intercept there). Long recompiled loops may need interrupt-poll checks (N64Recomp
  does this) — defer by keeping interrupt-sensitive code interpreted at first.
- **Cycle accounting** for recompiled functions (Section 4.3) — start with
  "untimed body, resync at boundary" and only make specific functions cycle-accurate
  if the diff harness flags a timing divergence.
- **Dynamic dispatch** (`JSR ($8197,x)`, `JSL` via tables) — already handled by
  `func_table_call_jsr`; the timed loop must route these through the same dispatch.
- **DSP-1 / SPC700** — keep as LakeSnes HLE/cycle-accurate; recompile only the *game
  code that uses* them, not the coprocessors themselves.
- **HDMA / per-scanline effects** — free in the timed loop (LakeSnes steps HDMA per
  line); this is exactly what the untimed shell couldn't do.
- **Self-modifying / bank-switched code** — the interp covers it; recompiled coverage
  is opportunistic.

## 8. Effort / risk

- **Phase 1 is the real lift** but is bounded: it's "run the existing timed emulator
  loop + add an entry-point dispatch hook," not a from-scratch CPU. Risk is mostly in
  the interception/cycle-resync details; the diff harness de-risks it.
- **Phases 3–4 are long but incremental and safe** (each function gated by the
  oracle). The project can ship the timed-recomp path as the default long before
  "100% recompiled."
- **Biggest open decision:** Option A vs B for *bulk* translation (Section 5) —
  worth a focused spike on `mstan/snesrecomp`'s recompiler before committing.

## 9. First concrete step

Prototype Phase 1 behind a flag (`SMK_RECOMP=1`): a `snes_runFrame`-style loop with a
single intercept — route one already-recompiled leaf function (e.g. `smk_80946E` OAM
DMA) through its `RECOMP_PATCH` while everything else interprets *with timing on* —
and confirm via the diff harness it stays bit-identical to `snes_runFrame` through a
race. That proves the interception + timed model end-to-end on one function; the rest
is repetition.

## 10. Phase-1 prototype — built & validated (2026-06)

Implemented behind `SMK_RECOMP=1` (off by default; zero overhead elsewhere):

- **CPU opcode-fetch hook** (`g_cpuRecompHook` in LakeSnes `cpu.c`, NULL by default so
  `lakesnes_ref` and real-frame are untouched). When set, it is consulted before each
  opcode in the timed loop.
- **Interception** (`recomp_timed_*` in `recomp_interp.c`): a registered entry address
  runs its recompiled C body (sync LakeSnes regs → `g_cpu`, run native, sync back) and
  PB:PC is advanced past the routine via a simulated RTS (near) / RTL (long), using the
  return frame the JSR/JSL left on the stack. Reuses the interp's `sync_to/from_lake`.
- **Host wiring** (`mk` `main.c`): `SMK_RECOMP=1` runs the real-frame timed loop with
  the hook on; `SMK_RECOMP_INTERCEPTS="80946E,81xxxx:L,…"` overrides the set (default
  the OAM-DMA leaf `$80:946E`). Per-60-frame `intercept_hits` + WRAM-checksum print.

**Result — the model works.** With `$80:946E` intercepted, the timed-recomp run is
**byte-identical to the pure-emulation oracle (WRAM+VRAM+CGRAM) for 86 consecutive
frames** (`tools/diff_snapshots.py`, `SMK_SNAPSHOT_EVERY=1`). The hook fires cleanly
(frame 3, correct JSR/RTS return, `db=$80` matches), and the game boots/renders for
180+ frames. Real-frame itself is fully deterministic run-to-run (control: `ref≡ref2`),
so the harness is a sound oracle.

**Finding 1 — sub-frame APU-timing fidelity is the real hurdle, not master-cycle
accounting.** From frame 87 a *tiny, intermittent* WRAM divergence appears (values like
`$54/$77/$D5` where the oracle has `$00`; heals and recurs) with **zero VRAM/CGRAM
impact** — an audio/handshake-buffer phase artifact. Because WRAM+VRAM+CGRAM are
identical for 86 frames before it, the differing state is *hidden* (OAM or, most likely,
**SPC700/APU interleaving**: the game polls `$2140-$2143` and the SPC's continuous
execution phase differs from how the instant native body consumed cycles). A one-shot
master-cycle compensation (`SMK_RECOMP_CYCLES`, swept 0→170) had **no effect**,
confirming it is interleaving-phase, not a simple cycle offset. → For hardware-/
APU-touching routines, fidelity needs the native body to advance the APU the way the
original instruction stream does (or keep such routines interpreted). Pure-logic
routines should be unaffected.

**Finding 2 — most existing `RECOMP_PATCH` bodies are shell-era approximations, not
faithful ports** (e.g. `smk_80853D` literally "simplified to just START"; `smk_80843C`
omits the original's `JSR`s). They cannot be bit-identical by construction. → Phase 3 is
not just "recompile more" but "make each body faithful, gated by this harness."
`smk_80946E` was the most faithful available and is the one used above.

**Conclusion.** The timed-loop + interception execution model is proven end-to-end: a
recompiled function plugged into the timed loop reproduces the oracle bit-for-bit for
dozens of frames. The remaining work is fidelity (faithful bodies + APU/sub-frame
timing for hardware-touching routines) and breadth — exactly Phase 3.
