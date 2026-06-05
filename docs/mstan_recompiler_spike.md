# Spike — mstan/snesrecomp automated recompiler for SMK

_Evaluation of [github.com/mstan/snesrecomp](https://github.com/mstan/snesrecomp) as
the bulk-translation lever for SMK (Option B from `recomp_frame_model_plan.md` §5).
Cloned to `D:\recomp\snes\mstan-snesrecomp`. 2026-06._

## What it is

A whole-ROM **ahead-of-time 65816→C recompiler** — the thing hand-porting can't scale to.
- `recompiler/` (Python, ~9.5k LOC): decoder + **abstract-interpretation M/X width
  inference** + autorouters (exit-(M,X), tail-call, wrapper-bypass, PHA-RTS, JSL-dispatch).
- `runner/` (C): its **own LakeSnes-derived** runtime (`runner/src/snes/ppu.c`, `apu.c`,
  `cart.c`) + a **cooperative-scheduler dispatch model** (no hardware stack for calls;
  per-(M,X) function variants; runtime dispatch table keyed by pc24).
- Status: **alpha**. SMW & Mega Man X *believed fully playable*; LttP partial. "APIs change
  without warning"; active stack-model bugs being fixed; **emitted stubs are a hard build
  error**.

## What I proved by running it on SMK

Ran `tools/v2_regen.py --rom "<SMK>.sfc"` on a 1-function cfg for `$85:84D1`
(`REP #$20 / INC $64 / SEP #$20 / RTS` — a function I'd already hand-ported).

1. **It runs on SMK out of the box** (fast) — but **mis-decoded**: the frontend is
   **LoROM-only**. `snes65816.py` has only `lorom_offset()` (asserts `addr ≥ $8000`); it
   read `$85:84D1` at the LoROM offset `0x284D1` and emitted garbage (`ROR/ORA…`). HiROM
   code below `$8000` would assert-crash outright. (The *runner* `cart.c` has HiROM; the
   *recompiler* does not.)
2. **HiROM is a trivial frontend fix.** I patched `lorom_offset` → `(bank&0x3F)*0x10000+addr`
   and re-ran. mstan then produced **correct** C: `REP #$20` → clears the M mirror, then
   **`cpu_read16`/+1/`cpu_write16` on `$0064`** — it *inferred the 16-bit width from the
   REP* — then `SEP`, then `RTS`. **Byte-for-byte the same logic as my hand-port.** The M/X
   inference (the slow part by hand) works on SMK.

So the **frontend genuinely handles SMK code**; the LoROM assumption is the only frontend
blocker and it's ~one function.

## The real costs / blockers for SMK

| Blocker | Severity |
|---|---|
| **LoROM-only frontend** | Low — 1-line HiROM offset (proven). |
| **No DSP-1 coprocessor** | Medium — SMK's Mode-7 race needs it; mstan's runtime has only the *audio* DSP. My LakeSnes fork **has** DSP-1 HLE → swap it into mstan's runner, or add DSP-1 there. |
| **SMK cfg authoring** | High (inherent) — per-bank entry lists + data-region carve-outs across ~16 banks. This is the per-game control-flow curation work. |
| **ABI / runtime model** | Generated C is `RecompReturn f(CpuState *cpu)` + per-(M,X) variants + cooperative return — **incompatible with my `g_cpu`/`op_*`/intercept-RTS model** as-is. |
| **Alpha instability** | Medium — unstable APIs, active bugs, session-context docs. |

## Notable upside

mstan's **cooperative-scheduler / dispatch** runtime does **not** run functions in "zero
emulated time" — it models the call/return/timing properly. That would **dissolve the
sub-frame-timing artifacts** my instant-execution intercept model hits (the dead-stack /
APU-phase divergences in `recomp_frame_model_plan.md` §10–11). And the auto M/X inference
is exactly the hand-porting bottleneck.

## Three ways to use it

- **Path A — harvest the *approach*, not the code.** Reimplement mstan's M/X
  abstract-interpretation + decoder in *my* toolchain, emitting *my* `RECOMP_PATCH`/`op_*`
  style, gated by my diff harness. Keeps LakeSnes/DSP-1/intercept/menu/netplay. Cost: I
  re-build a flag-aware codegen (mstan's is 9.5k LOC; a useful subset is less).
- **Path B — re-platform SMK onto mstan's framework.** Add HiROM (done-in-spike), swap my
  LakeSnes in for DSP-1, author the SMK cfg, new SMK runner repo. Highest ceiling (full
  auto-recompile + the scheduler fixes timing) but multi-week, alpha-framework risk;
  **subsumes** the intercept model + menu/netplay (re-integration needed).
- **Path C — adapter (novel, lowest-effort to *try*).** Implement mstan's small generated
  ABI (`CpuState`, `cpu_read8/16`, `cpu_write8/16`, `RecompReturn`) as a **shim over my
  `g_cpu` + LakeSnes bus**, so mstan-*generated* functions are callable from my existing
  intercept hook. Reuses mstan's codegen directly; keeps my runtime + DSP-1. Risk: the
  per-(M,X) variants + cooperative-return semantics may not map cleanly onto single-shot
  intercept for non-leaf functions.

## Recommendation

The spike **de-risks Option B substantially** — the frontend works on SMK and HiROM is
trivial. The decision is a genuine strategic fork:

- For **steady momentum keeping everything that works**, **Path C** is the cheapest *first
  experiment*: stand up the mstan-ABI shim and try one mstan-generated SMK leaf through my
  intercept hook + diff gate. If the ABI shim is clean, it gives auto-translation *into my
  model* without a re-platform.
- For the **highest ceiling** (and to escape the instant-execution timing ceiling),
  **Path B** is viable now that the frontend is proven — but it's a multi-week commitment to
  an alpha framework and replaces the current model.

Either way, my diff harness (`tools/diff_snapshots.py` vs `lakesnes_ref`) validates the
output identically — the safety net carries over.
