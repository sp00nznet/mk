# Building our own auto-generator (inspired by SuperRecomp, not mstan)

_Assessment of [github.com/dreamsailing59-ops/SuperRecomp](https://github.com/dreamsailing59-ops/SuperRecomp)
(a fork of ExpansionPak/SuperRecomp) and a concrete design for an SMK auto-porter in
**our own** toolchain. Cloned to `D:\recomp\snes\SuperRecomp`. 2026-06._

## SuperRecomp as a tool: not usable

**Abandoned, alpha, SMW-only** (its own README: "only game that currently works is Super
Mario World"; other ROMs emit empty C++; "needs polishing to compile without errors").
~950 LOC of C++. Far less mature than mstan. So we can't *use* it — but its code is small,
readable, and its structure is directly applicable.

## The key insight: its runtime model **is ours**

SuperRecomp's generated code targets the *exact* shape `snesrecomp` already has:

| SuperRecomp | our snesrecomp |
|---|---|
| `std::map<uint32_t, RecompiledFunc> function_table` | `func_table` |
| global `CPURegs regs` | global `g_cpu` |
| per-opcode helpers `LDA_imm`/`STA_dp`/`REP`… | `op_lda_imm16`/`op_sta_dp`/`op_rep`… |
| `snes_memory_read/write` | `bus_read8/write8` |
| `sub_0xADDR()` functions | `RECOMP_PATCH(smk_ADDR, …)` |

So an auto-generator that emits *our* `RECOMP_PATCH` bodies is not a re-platform — it's a
codegen layer over the runtime we already have and the validated intercept model.

## What SuperRecomp does (patterns worth borrowing)

1. **Two-pass recursive-descent.** Pass 1: worklist of addresses; walk instructions, push
   discovered branch/JSR/JSL targets, stop at RTS/RTL. Pass 2: emit one C function per
   discovered address.
2. **Per-opcode → helper-call emit.** `emitC` is a `switch(op)` that prints one statement
   per instruction calling the matching helper. Trivial to retarget to our `op_*`.
3. **M/X state threading** (`state.is16A` / `state.is16XY`, flipped by REP/SEP) to size
   immediates — same idea as our `disasm65816.py`.
4. **Trampoline control flow:** branches/tail-calls become `next_func_addr = target; return;`
   driven by an outer dispatch loop, and `JSR`→`sub_0xT()`, `RTS`→`return;`. Sidesteps
   structured-CF reconstruction.

## Why it (and naive auto-gen) stays SMW-only — and how we beat it

- **Incomplete opcode table** — hand-built per the opcodes SMW happens to use. *We already
  have a fuller M/X-aware decoder* (`disasm65816.py` decodes SMK).
- **Entry-(M,X) guessing.** SuperRecomp threads M/X from a default; mstan needs per-variant
  fixpoint because a function can be entered with different widths from different callers.
  **Our unique advantage: the LakeSnes range-tracer gives the EXACT observed entry flags per
  function** (`SMK_TRACE_RANGE`, prints `p=`). That sidesteps the hardest problem for every
  function actually executed.
- **No validation.** We have the oracle (`tools/diff_snapshots.py --ignore-wram 1F00-1FFF`
  vs `lakesnes_ref`) — every generated function gated byte-identical.

## Proposed design — `tools/recomp/autogen.py`

A Python codegen in our stack, borrowing SuperRecomp's structure, fed by our trace + gated
by our harness:

```
input:  (entry pc24, entry M, entry X)   # M/X from the tracer
walk:   reuse disasm65816.py's decoder; thread M/X through SEP/REP/PLP
emit:   RECOMP_PATCH(smk_ADDR, 0xADDR) {
          <one line per instruction>      # opcode -> op_* call (or inline bus+flag)
        }
gate:   build + diff_snapshots.py; keep only if byte-identical (stack-masked)
```

Two codegen styles for the body (decide per the helper coverage we want):
- **(A) helper-call** — emit `op_<mnem>_<mode><width>(operand)`. Clean, but needs an `op_*`
  per (opcode, addr-mode, width). Our library is partial → expand it SuperRecomp-style.
- **(B) inline** — emit `bus_read16/write16` + flag updates directly (mstan-style). No helper
  gaps; more verbose. Best for the addressing modes with no `op_*` (e.g. `[dp],Y`).

Recommend a **hybrid**: helper-call where an `op_*` exists, inline otherwise.

**Scope honestly:** v1 targets **straight-line / simple-branch leaf functions** — exactly the
bulk of the hand-porting bottleneck (e.g. `smk_8584D1`, `smk_8181C4`, `smk_858FB8` were all
straight-line). Hard cases (indirect dispatch, computed jumps, multi-entry, varying-flag
entry, APU/DMA-touching) stay interpreted or hand-ported — same as today. The auto-gen just
removes the tedious 80%.

**First milestone:** regenerate an already-hand-ported leaf (`smk_8584D1`) from its bytes +
traced entry flags and diff the generated function against the hand-port — proving the
codegen on a known-good target before scaling.

## Bottom line

SuperRecomp isn't usable, but it **confirms our runtime is the right shape for
auto-generation** and hands us a concrete codegen template. Combined with our two assets
SuperRecomp/mstan lack — **trace-exact entry flags** and a **validation oracle** — a small
auto-generator in our own toolchain (no mstan code) can automate the leaf-porting bottleneck
while keeping LakeSnes/DSP-1/intercept/menu/netplay intact.

## Built & results (2026-06)

`tools/recomp/autogen.py` + `tools/recomp/batch.py` exist and work:

- **autogen.py** decodes a function from ROM + trace-exact entry M/X (CFG walk, M/X
  threaded), and emits a `RECOMP_PATCH`: register/stack/flag ops via `op_*`; loads/stores/
  STZ/logical/compare/inc-dec/shift/BIT across all common addressing modes via inline
  `bus_read/write` + flags; JSR/JSL and computed jumps via `func_table_call`; branches via
  labels/gotos. Per-(M,X) re-entry remains `Unsupported` (clean fallback).
- **batch.py** runs autogen over a profile's `PROF <addr> <count> <P> [MULTI-MX]` lines
  (the profiler now records each call target's entry M/X), reports yield + skip reasons.
- **Profiled through a real race** (input script `360:Y` + mash `START` to `$36=02`, then
  hold `B` to drive). Batch generated 26 functions; **16 validated byte-identical to the
  emulation oracle THROUGH THE RACE** — including the **Mode-7 engine** (`$818902`,
  `$81B9A8`) and the race object loop (`$80F90A`). Default `SMK_RECOMP=1` intercepts all 16.

### Findings

- **Link-anchor bug (fixed).** `smk_autogen.c` is a static-lib TU with no
  externally-referenced symbol → the linker dropped it and its `RECOMP_PATCH` constructors
  never ran. Always check `intercept_hits > 0` — byte-identical with zero hits is a non-test.
- **No combined-interception count ceiling for pure-logic functions.** Initially looked like
  a ~dozen-function ceiling, but a `SMK_RECOMP_CYCLES` sweep showed the 16 pure-logic
  functions compose byte-identical at zero cycle-advance (adding cycles *hurts*). The real
  exceptions are **timing-sensitive functions**: hardware-register readers (the `$4218`
  input handler `$808445`, read-phase-dependent) and **functions called during transitions**
  (race start `$0080:` APU-phase window, e.g. `$8181C4` at f960). Those are the instant-
  execution ceiling (§10–11) and need cycle-accurate execution — not re-seeding. The 10
  batch FAILs are these, not emit bugs.

So the auto-generator reaches gameplay/Mode-7 code and proves it function-by-function against
the oracle; steady-state pure-logic composes without limit, while transition/IO-timing code
is the boundary that motivates the timed/cycle-accurate model.

## Correctness pass (2026-08)

- Fixed CFGs whose real entry is above a shared lower-address block. Emission stays in address
  order for fallthrough, but every generated function now jumps explicitly to its true entry.
  `$80:8EED` is the real-ROM regression case: without the entry jump it returned through
  `$80:8EEA`; with the fix it passed 1,400 frames / 2,030 observed calls byte-identically.
- Added width-correct 8/16-bit PHA/PLA/PHX/PLX/PHY/PLY emission and stack-cycle costs, plus
  PHK/PHD/PLD, TSB/TRB, and TSX/TXS/TXY/TYX/TCD/TDC/TCS/TSC. Seven synthetic CFG/opcode tests
  cover the new rules and REP/SEP width changes.
- Added `$81:BB70` (738 profiled calls) and `$80:8EED` to the generated/default set. The
  18-function default made 6,801 native interceptions during a scripted 1,400-frame run and
  matched interpreter WRAM/VRAM/CGRAM at every sampled frame.
- Re-gating also corrected an optimistic historical classification: `$80:9EB2` diverges at
  race init through its `$80:9FAC` callee, while `$81:81C4` and `$85:92F9` are transition
  sensitive. Their generated bodies remain available for opt-in experiments, but they are no
  longer enabled by default.
- Added binary/decimal-correct 8/16-bit ADC/SBC emission for the generator's existing memory
  modes, plus NOP. This raised the generated set from 21 to 31 bodies. `$81:F638` and its eight
  indirect JSR targets demonstrate the next boundary: the C semantics generate, but approximate
  instruction timing reaches the race-init NMI boundary at a different point. The closure is an
  opt-in regression for the bus-phase/exact-interrupt milestone, not part of the safe default.
- Corrected 32 shared opcode-table sizes: `($dp,X)`, `($dp)`, `($dp),Y`, `[$dp]`, and
  `[$dp],Y` all carry one operand byte. Added their 24-bit effective-address helpers together
  with stack-relative `$dp,S` and `($dp,S),Y`, including bank-00 pointer wrap semantics.
- Added `MVN`/`MVP` generation with per-byte ticking, data-bank update, direction, accumulator
  count, and index-width behavior. The generated set is now 32 bodies.
- CFG nodes are keyed by `(PC,M,X)` and use explicit successor gotos, so internal width-state
  merges no longer misdecode immediates. The profiler now emits exact entry combinations such as
  `VARIANTS=00,20`; generated multi-entry functions dispatch from live M/X flags without trying
  impossible combinations. This matters for the SPC700 upload routines `$81:F504`/`$81:F4B2`,
  where inventing X=8 turns a 16-bit immediate's high byte into a false `BRK`.
- Full standard-route audit: 361 distinct direct targets, 56 already registered and all 305
  remaining candidates generated (`unsupported: 0`). Fifteen focused generator tests pass; the
  Release build passes; the 18-function default still makes 6,801 interceptions and matches all
  140 reference snapshots across the 1,400-frame route.
- M3 started with path-dependent timing: taken conditional branches and `BRA`/`BRL` add their
  missing internal cycle. Sixteen generator tests pass and the default 18-function oracle gate
  remains green. `$81:F638` still diverges first at frame 1040, proving the remaining fault is
  phase/interrupt placement rather than a missing constant branch cost.
- Added an opt-in bus-phase runtime (`SMK_RECOMP_BUSPHASE=1`). Generated instructions perform
  timed ROM fetches, timed data reads/writes at their live addresses, internal idle phases, and
  synthetic RTS/RTL stack phases; the flag-off path retains the validated aggregate model.
  `JSR (abs,X)` now includes its two previously missing jump-table reads. The prototype is stable
  and generated nested calls now materialize their return PC on the emulated stack before they
  can yield. Aggregate totals are pinned by tests at RTS=40, RTL=42, and FastROM
  `JSR (abs,X)`=52 master cycles.
- Closed the `$81:F638` timing regression as a full C call closure. The original red run left
  `SMK_INTERP` at its default ON setting, so all eight registered children used the untimed
  interpreter and made every observed standard-route root call exactly 134 master cycles short.
  With `SMK_INTERP=0`,
  bus-phase timing and native children enabled, 2,715 root interceptions match every one of the
  140 standard-route snapshots through frame 1400 byte-for-byte, including stack WRAM. The
  default 18-function gate also remains green when its documented dead stack scratch is ignored.
  Twenty generator tests pass. General checkInt micro-phases and exact stack/RMW/JSL bus ordering
  are still M3 work; this result proves the first difficult closure, not the whole timing model.
- Replaced the generated-call integer byte count with an explicit caller-SP/PB frame token.
  Aggregate JSL now enters and restores the target program bank correctly; bus-phase interpreter
  fallback consumes the already-materialized frame rather than pushing a second sentinel. The
  fallback temporarily suspends the timed recomp hook and preserves pending interrupt semantics,
  so force/depth fallback no longer recursively intercepts itself. Re-gating the default set now
  executes 11,794 native interceptions (the earlier 6,801 count was before PB correction) while
  retaining byte-identical state outside `$1F00-$1FFF`; the exact `$81:F638` closure remains
  byte-identical including stack WRAM for all 2,715 calls.
- Split interrupt sampling from instruction-boundary yielding and encoded LakeSnes `checkInt`
  positions in generated code. Checked 8/16-bit bus helpers sample before a byte or between word
  bytes; RMW writes use the native high/check/low order; branch sampling depends on the taken
  path; and ordered stack/call/return helpers cover PHA/PLA-family operations, JSR/JSL, RTS, and
  RTL. The suite now has 26 focused generator tests. Rebuilding all 32 generated functions keeps
  the `$81:F638` closure raw-byte-identical for 140/140 snapshots and the default 18-function
  set byte-identical outside its documented dead stack scratch.
- Split the two non-linear call fetch sequences. JSL now delays its bank operand until after the
  old PB push and idle; `JSR (abs,X)` pushes its return word between the low and high operand
  fetches, then idles before the checked jump-table word. The F638 closure executes that indirect
  sequence twice per root invocation and remains raw-byte-identical across 2,715 calls. JSL has
  generator/build coverage, while a fully registered child closure is still needed for its
  ROM-level timing gate.
- Added bus-phase emission for direct JMP/JML and all three indirect tail forms. Checks now occur
  between the correct operand/table bytes, `JMP (abs,X)` idles before its checked table read, and
  JML `[abs]` samples before the bank byte. Direct targets already decoded in the same CFG become
  local C gotos; this removes three unnecessary fallback exits in the generated set. Thirty
  generator tests and both standard ROM gates remain green; the default set's only raw changes
  are dead stack scratch caused by avoiding those fallback return frames.
- Replaced aggregate MVN/MVP loops with one bus-visible iteration per C CFG pass. Each byte now
  refetches the three-byte instruction, sets DB before the source read, performs the destination
  write, updates A/X/Y, and ends with LakeSnes's idle/check/idle sequence. A timing yield resumes
  at the block-move opcode until A wraps, then at the following PC. RTI now idles twice, restores
  P and PC, samples interrupts with the restored I flag, pulls PB, and leaves through the redirect
  protocol rather than the interception hook's synthetic RTS/RTL. `cpu_set_p` also enforces E-mode
  M/X and immediate X/Y narrowing. Thirty-one generator tests, the Release build, the 2,715-hit
  raw F638 gate, and the 11,794-hit default gate remain green. The standard 32 generated functions
  contain none of MVN/MVP/RTI, so focused synthetic coverage is not yet a ROM-level proof.
- Added dynamic addressing-mode penalties in their LakeSnes bus positions. Direct-page forms idle
  when DP's low byte is nonzero; DP-indexed and stack-relative forms perform their fixed idles;
  `(dp),Y` and absolute indexed reads test live X width and page crossing, while writes/RMW always
  idle. Indirect helpers keep penalties on the correct side of pointer reads. The shared runtime
  helper becomes a CPU idle in bus-phase mode and an aggregate tick otherwise, so generated code
  does not double-count either path. Regeneration adds 224 potential penalty sites to the standard
  set. Thirty-four generator tests, Release, the raw 2,715-hit F638 gate, and the functional
  11,794-hit default gate all pass; the latter's remaining raw bytes stay solely in dead stack
  scratch `$1F00-$1FFF`.
- Replaced the hand-written `$80:946E` OAM-DMA approximation with a generated bus-phase body.
  The `STA $420B` trigger, following opcode fetch, and following operand fetch now traverse
  LakeSnes separately, so its deferred DMA state machine stalls the CPU before `REP` executes;
  the same direct bus calls preserve LakeSnes open-bus updates. The focused gate executes 1,071
  interceptions and matches 140/140 raw snapshots through frame 1,400, superseding the old
  86-frame limit. Thirty-five generator tests, Release, the 2,715-hit raw F638 gate, and the
  11,794-hit functional default gate remain green. The aggregate path and internal-latch-only
  assertions remain outside this exact claim.
