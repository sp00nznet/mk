# Roadmap to a fully C-recompiled Super Mario Kart

This document distinguishes the playable emulator-backed launcher from the long-term
static-recompilation goal. "Fully C-recompiled" means that all reachable 65C816 game-code
paths execute as native C, while the original ROM is used only for data tables/assets and
LakeSnes remains the emulated SNES hardware backend. It does **not** mean rewriting the PPU,
SPC700, DSP-1, DMA, or SDL platform layers.

## Measured baseline (2026-08-23)

The standard 1,400-frame scripted route covers title, driver select, class/cup selection,
race initialization, and a short driving segment.

| Metric | Current value | Meaning |
|---|---:|---|
| Registered C entries | 81 | 49 hand-written plus 32 auto-generated; registration alone does not imply correctness |
| Default oracle-gated intercepts | 18 | Safe as one combined set on the standard route |
| Distinct direct JSR/JSL targets on the route | 361 | Excludes indirect calls, jumps, interrupt entries, and unvisited game modes |
| Registered direct targets reached | 56 / 361 (15.5%) | Potential C coverage if every registered body were enabled |
| Direct calls to registered targets | 86,972 / 150,979 (57.6%) | Call-weighted potential, boosted by the opt-in APU reader `$81:F56C`; not the default enabled share |
| Default execution mode | ROM interpreter | `snes_runFrame` still executes the original 65C816 code |

The percentages above are **route coverage**, not whole-game completion. Two-player modes,
all cups/tracks, battle mode, attract/demo paths, results/credits, exceptional input paths,
indirect dispatch targets, and interrupt/fallthrough entries are not fully represented.
Consequently the defensible whole-project statement is: infrastructure is established and a
small validated subset exists, but the project is still in an early static-recompilation phase.

## Current technical boundary

The generator now handles ordinary control flow, direct and computed calls/jumps, width-aware
stack/register operations, TSB/TRB, binary/decimal 8/16-bit ADC/SBC, all observed direct-page
indirect and stack-relative modes, `MVN`/`MVP`, and distinct `(PC,M,X)` CFG states. The profiler
exports exact multi-width entry variants and the generator selects the matching entry block from
the live CPU flags. On the standard route, all 305 not-yet-registered direct targets generate;
combined with 56 registered targets this is 361/361 observed-target generation coverage.
Important gaps remain:

- general exact timing: the remaining interrupt recognition cases, aggregate-path DMA/open-bus
  approximations, direct internal-latch validation, and ROM closures for the newly ordered
  block-move/RTI/control cases;
- complete discovery of indirect call-table targets and all entry flag variants;
- a C-owned reset/NMI/main-frame scheduler that can finish race initialization without
  falling back to `snes_runFrame`;
- scenario coverage for all game modes and long-running races.

The first hard nested-call timing regression is now green: `$81:F638` plus all eight indirect
JSR targets execute as a true C closure for 2,715 intercepted calls and match WRAM, VRAM, and
CGRAM across all 140 sampled frames of the standard route. This requires
`SMK_INTERP=0 SMK_RECOMP_BUSPHASE=1 SMK_RECOMP_PHASE_YIELD=1`; leaving the default interpreter
force enabled sends each registered child through the untimed fallback and, in the observed
standard-route `$81:F638` burst, loses exactly 134 master cycles per call. The closure remains
opt-in while the bus-phase runtime is generalized.
`$85:96DC` still has two transition-only divergence frames (1010 and 1030) and is not
default-enabled.

## Milestones and acceptance gates

### M1 — Reproducible baseline and observability

- Keep ROM hashes, build instructions, deterministic input scripts, call profiles, and
  WRAM/VRAM/CGRAM snapshot comparison reproducible.
- Export the complete profile with `SMK_RECOMP_PROFILE_TOP=1000`.
- Gate every default-set change on the standard 1,400-frame comparison.

**Exit gate:** a clean checkout plus a user-supplied matching ROM can build and reproduce the
same profile and byte-identical reference snapshots.

### M2 — Generator instruction/addressing completeness

- Implement the missing indirect and stack-relative effective-address modes.
- Implement `MVN`/`MVP`, remaining control/stack instructions, and accurate emulation-mode
  behavior.
- Split CFGs by `(M,X)` state instead of rejecting multi-width re-entry.
- Add focused unit cases for every opcode/addressing/width rule.

**Exit gate (met 2026-08-23):** every direct-call target from the standard profile, including
multi-width entries, either already has C registered or generates successfully. The 1,400-frame
default-set regression remains byte-identical across all 140 sampled frames after the M2 changes.

### M3 — Exact timed native execution

Progress: taken conditional branches and `BRA`/`BRL` pay their dynamic internal-cycle penalty.
The aggregate model now includes complete return and computed-call totals (`RTS=40`, `RTL=42`,
FastROM `JSR (abs,X)=52` master cycles). An opt-in `SMK_RECOMP_BUSPHASE=1` runtime places
opcode/operand fetches, data accesses, internal idles, and return-stack phases on the LakeSnes
clock. Nested generated JSR/JSL calls carry a caller-SP/PB token and materialize their emulated
return frames in bus-phase mode. Cross-bank JSL also switches/restores PB in aggregate mode. A
token-aware fallback consumes an existing physical frame instead of pushing a second sentinel,
preserves live interrupt state, and suspends the timed hook to avoid recursive interception.
Instruction boundaries can redirect back to the remaining ROM PC when a frame boundary is crossed.

The next M3 layer now mirrors LakeSnes's explicit `checkInt` micro-phases for the generated
instruction families used by the standard set. Immediate operands sample before an 8-bit value
or between 16-bit bytes; data reads/writes use the equivalent before/between access point; RMW
instructions idle and sample between their reversed writes; taken/not-taken branches select
different fetch phases; and stack, JSR/JSL, RTS, and RTL paths sample between their documented
push/pull phases. Sampling only latches LakeSnes `intWanted`; yielding remains an instruction-
boundary operation, so a partially executed C instruction is never exposed.

Call fetch ordering is now split where the 65C816 sequence is non-linear. JSL fetches only its
opcode and 16-bit address before pushing PB, idles, fetches the bank operand, then pushes the
checked return word. `JSR (abs,X)` fetches opcode+operand-low, pushes its unchecked return word,
fetches operand-high, idles, and performs the checked table-word read. The F638 closure exercises
the latter twice per root call and remains byte-identical; the JSL sequence is structurally tested
and compiled. The September `$81FD22 -> $81F638` expansion now provides an all-C JSL child
closure gate: 2,684 root interceptions and all 140 raw snapshots match.

All five JMP/JML forms now participate in bus-phase timing. Direct JMP/JML sample between their
documented operand bytes; indirect forms preserve same-bank pointer wrapping, indexed idle
placement, checked word reads, and JML's low/high/check/bank sequence. Three direct JMP targets
already present in the same generated CFG now remain as C `goto` edges instead of leaving through
the fallback table. The standard generated set contains five direct JMPs and one indexed indirect
JMP; all compile, while a fully registered indirect-tail target closure is still needed for an
isolated bus-phase ROM gate.

The `$81:F638` eight-target closure now passes the first M3 hard gate. The earlier apparent
frame-1040 failure was a validation configuration error: with the default `SMK_INTERP=ON`, each
registered indirect child ran through the untimed interpreter fallback. Its six-instruction
path costs exactly 134 master cycles, matching the measured per-call drift. With
`SMK_INTERP=0`, entry timing, registers, stack state, and frame-end PC/H/V positions align with
the ROM oracle, and all 140 sampled snapshots through frame 1400 are byte-identical without an
ignored region. The default 18-function set also remains byte-identical outside its documented
dead stack-scratch range; after the PB/fallback correction it executes 11,794 native
interceptions on the same route (up from the earlier 6,801 checkpoint).

MVN/MVP now run as individually timed hardware iterations: each byte refetches the instruction,
sets DB before source access, performs the ordered read/write and register updates, then executes
idle/check/idle. A boundary yield resumes at the opcode while A has not wrapped. RTI now restores
P/PC/PB using the LakeSnes idle/pull/check sequence and redirects without a synthetic RTS/RTL;
status restoration also enforces emulation-mode M/X and index narrowing. These opcodes have
focused synthetic tests but are absent from the current 37 generated functions, so a real ROM
closure containing them remains an explicit gate rather than an inferred success.

Generated addressing modes now reproduce LakeSnes's runtime-dependent idle phases. A nonzero DP
low byte adds its idle before direct-page address/pointer access; DP-indexed forms add their fixed
index idle; `(dp),Y` distinguishes reads from writes and tests X width/page crossing after its
pointer word; absolute indexed reads use the same live-width/page test while writes and RMW forms
always idle; and stack-relative indirect places one idle on each side of its pointer read. A new
runtime penalty helper performs a true CPU idle in bus-phase mode and an extra aggregate tick
otherwise. Regenerating the standard set emits 224 potential penalty sites.

The hardware-touching `$80:946E` OAM-DMA leaf is now generated instead of using its old instant
hand-written body. Its write to `$420B` and the next instruction's opcode/operand fetches are
separate LakeSnes bus operations, preserving the emulator's deferred DMA arm/execute sequence.
The focused gate executes 1,071 native interceptions and matches all 140 raw snapshots through
frame 1,400. Bus-phase reads and writes also inherit LakeSnes's open-bus updates directly. This
closes the exercised DMA-stall case, while the aggregate path remains approximate and the
snapshot format still does not directly assert internal open-bus/DMA latch state.

This is a closure-specific success, not completion of M3. The new micro-phase layer is covered by
35 generator tests, and the `$81:F638` closure still passes all 140 raw snapshots after the
change. The default 18-function gate also remains identical outside `$1F00-$1FFF` across all 140
samples. `SMK_RECOMP_PHASE_YIELD=1` remains experimental until the remaining interrupt,
internal-latch, aggregate-path, and unexercised control cases are modeled and gated. The fallback call-frame
protocol is stack-safe, but fallback execution remains intentionally untimed and therefore is not
part of the exact-closure claim.

- Move from aggregate instruction costs to bus-phase-accurate fetch/read/write/idle timing.
- Allow NMI/IRQ recognition at the same instruction boundaries as LakeSnes.
- Carry timing through nested native calls and indirect call closures.
- Use `$81:F638` plus its eight-target closure as the first hard regression.

**Exit gate:** all pure-logic and hardware-touching leaf closures on the standard route compose
without ignored divergences.

### M4 — Whole call-graph migration

- Generate/port functions by strongly connected call-graph component, not isolated entries.
- Discover indirect targets from runtime traces and ROM pointer tables.
- Keep the interpreter only as a diagnostic oracle; fail loudly if a production C path requests
  an unregistered code address.

**Exit gate:** title-to-race driving completes with zero interpreter-dispatched opcodes while
WRAM/VRAM/CGRAM remain oracle-identical.

### M5 — C-owned frame scheduler

- Replace the default `snes_runFrame` CPU execution with the recompiled reset, main, NMI, IRQ,
  DMA/HDMA coordination, and wait-state model.
- Keep LakeSnes only for hardware devices and bus routing.

**Exit gate:** the default launcher boots and drives a race with the LakeSnes 65C816 CPU disabled.

### M6 — Whole-game coverage and ROM-code independence

- Add deterministic scripts/save-state seeds for every cup/track, 50/100/150cc, battle,
  two-player, results, credits, demo/attract, pause, retry, and save-data paths.
- Classify every ROM byte used as code versus data; prohibit execution from ROM in release mode.
- Run long-duration and randomized-input differential tests.

**Exit gate:** no reachable 65C816 ROM code executes across the scenario matrix; the ROM remains
required only for legal user-supplied data/assets, with all compared state and rendered output
matching the oracle.
