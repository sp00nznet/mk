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
  `bus_read/write` + flags; JSR/JSL via `func_table_call`; branches via labels/gotos.
  Indirect dispatch / per-(M,X) re-entry → `Unsupported` (clean fallback).
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
