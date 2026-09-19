# Related projects and a coverage-first plan

Research checkpoint: 2026-09-12. These are repository-documentation findings, not
claims that we built or verified those projects. No external source or assets
were imported during this investigation.

## Projects relevant to the original SNES game

| Project | What its repository provides | Relevance to this contribution |
| --- | --- | --- |
| [Yoshifanatic1/Super-Mario-Kart-Disassembly](https://github.com/Yoshifanatic1/Super-Mario-Kart-Disassembly/tree/main/SMK) | Assembly project with ROM/RAM maps, routine macros, Asar scripts and SPC700 files | Useful for naming and understanding addresses; assembly reconstruction is not native C execution. Check individual material before reusing it. |
| [jvipond/super_mario_kart_disassembly](https://github.com/jvipond/super_mario_kart_disassembly) | Python tooling driven by modified Snes9x instruction traces; README describes Asar rebuilding a matching ROM | Supports trace-guided discovery of code and entry widths, with explicit reconstruction validation. |
| [jvipond/super_mario_kart_recompilation](https://github.com/jvipond/super_mario_kart_recompilation) | LLVM attempt consuming the disassembler's JSON AST | README explicitly says emitted bitcode only starts register allocation and labeled basic blocks; not evidence of a completed native port. An IR rewrite would add substantial infrastructure before improving game coverage. |
| [MrL314/smk-spc700-disassembly](https://github.com/MrL314/smk-spc700-disassembly) | Audio-driver extraction, assembly and annotation-patch tools using a supplied ROM | Valuable for a later audio milestone and reproducible annotation workflows; not a 65C816 game-logic recompile. |
| [MrL314/Project-L](https://github.com/MrL314/Project-L) | Third-party SNES assembler/linker and reconstruction tooling | Its setup requires additional source archives beyond a cartridge dump. Not selected as the basis for this ROM-derived C workflow. |
| [vmbatlle/super-mario-kart](https://github.com/vmbatlle/super-mario-kart) | C++11/SFML gameplay clone, five circuits and circuit-generation utilities; GPLv3 | Useful as a model of gameplay module boundaries. It reimplements gameplay rather than proving equivalence to the ROM; importing it would change our objective and language. |
| [ScottStevenson/SuperAstra](https://github.com/ScottStevenson/SuperAstra) | Desktop companion for investigating and changing games running in BizHawk | Search results can conflate live editing with recompilation. Its current README describes emulator-assisted investigation, not a completed SMK C port. |

The inspected repositories do not establish the existence of a finished,
drop-in, all-C recompile of the original SNES game. That is a conclusion about
this search, not a claim that no such project can exist.

## Change in priorities

Previous work made bus timing more faithful, but only 33 generated routines were
linked and the default interception set still contained 18 roots. Generating all
observed targets offline did not automatically make them run as C in the game.

The next increments therefore prioritize complete, frequently executed call
chains. Use the existing generator and hardware backend; select candidates from
the ROM profile, audit their calls and returns, link their C bodies, then gate
them individually and together. Fix timing when a concrete chain exposes a
failure. Maintain separate counts for generated entries, executed roots, and
fully covered call chains; none is a whole-game completion percentage.

The first batch contains `$81:F722`, `$81:FD22`, `$80:87D9`, and `$80:879A`.
All four roots return via RTS. `$81:FD22` calls the already generated
`$81:F638` RTL closure, and `$80:879A` calls `$80:87D9`. The other two roots have
no emitted function-table calls. Addresses and entry widths come from local ROM
analysis and the recorded profile; descriptive gameplay names remain to be
established rather than guessed.

## Reproducible gate

After building Release, run from the repository root:

```sh
python tools/recomp/validate_route.py
```

The tool creates a fresh directory under ignored `build-manifest/closure-gates`,
runs the reference and native paths for 1,400 frames using identical scripted
input, requires every expected snapshot and nonzero completed native
interceptions, and compares WRAM/VRAM/CGRAM without ignored regions by default.
It records executable/ROM hashes, input, interception selection, hit count and
all differing sampled regions in `report.json`. A process error, timeout,
incomplete run or mismatch fails the command. `--intercepts ADDRESS` selects an
individual root; `--intercepts '' --ignore-dead-stack` gates the existing default
set with its historical stack exclusion. Raw snapshots remain local.

## Measured result

Release build and the existing 35 generator tests passed. Fresh 1,400-frame runs
gave the following results (140 expected samples each):

| Selected roots | Completed interceptions | Snapshot gate |
| --- | ---: | --- |
| `81F722` | 4,096 | Raw byte-identical |
| `81FD22` | 2,684 | Raw byte-identical |
| `8087D9` | 2,440 | Raw byte-identical |
| `80879A` | 2,440 | Raw byte-identical |
| All four new roots | 9,220 | Raw byte-identical |
| Legacy 18 plus four new roots | 21,014 | Identical outside `$1F00-$1FFF` |

Individual counts must not be added to predict the combined count: calls to
`8087D9` from native `80879A` run as nested C calls, not as another intercepted
ROM root. The combined preset increases root interceptions by 78.2% over the
previous 11,794-hit checkpoint. This is an execution-count increase, not a speed
measurement or a percentage of the game's source recovered.

The launcher accepts `SMK_RECOMP_INTERCEPTS=coverage` for the 22-root preset. For
its verified configuration use `SMK_RECOMP=1`, `SMK_INTERP=0`,
`SMK_RECOMP_BUSPHASE=1`, and `SMK_RECOMP_PHASE_YIELD=1`. The legacy default remains
18 roots because these new roots were gated with bus phases, not aggregate timing.
The same preset can be verified with:

```sh
python tools/recomp/validate_route.py --intercepts coverage --ignore-dead-stack
```

There are now 85 registered C entries, 37 generated. The strict new batch also
supplies a concrete JSL-to-native-child gate through `81FD22 -> 81F638`.
Neither snapshots nor interception counts prove every branch was exercised;
OAM, CPU/internal latches, audio state, and all tracks/modes are outside this
snapshot gate. LakeSnes still executes the remaining game code and owns frames.

## Following milestones

1. Continue expanding tested C call chains from the new selectable combined set.
2. Add per-function execution/fallback counters so coverage is measured by work
   actually executed; extend the route beyond one title-to-race scenario.
3. Give validated functions semantic names and compact state documentation using
   independently checked ROM/RAM mappings.
4. Advance C ownership to the frame dispatcher and interrupt entry once its
   callees are covered. Reset/NMI/frame scheduling still belongs to LakeSnes;
   replacing this remains required for the complete recompilation objective.
