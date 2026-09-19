# Recompiler correctness contribution branch

This document describes the public `contrib/recompiler-correctness` branch in the
[`Zaxaerith/mk`](https://github.com/Zaxaerith/mk) fork. The branch is based on
[`sp00nznet/mk`](https://github.com/sp00nznet/mk) and is intended for study, validation, and
incremental contribution toward a complete WDC 65C816-to-C recompilation.

## Scope and current state

The project is playable, but playability and recompilation completeness are different metrics:

- The default **real-frame** path runs the user-supplied game ROM on LakeSnes and is playable
  end-to-end.
- The **recompiled-shell** and **timed-recomp** paths execute selected translated C functions and
  are the paths being expanded toward full C ownership.
- A standard deterministic 1,400-frame title-to-race route observes 361 direct call targets.
  Fifty-six already have registered C entries and all remaining 305 generate successfully for
  their observed M/X entry variants (`unsupported: 0`).
- The repository currently registers 85 C entries, including 37 generated functions. Only
  oracle-gated subsets are enabled by default; generation success alone is not treated as proof
  of runtime correctness.

The first difficult exact-timing closure is now green. `$81:F638` and all eight of its indirect
JSR targets execute as native C with bus-phase timing. Across 2,715 root interceptions, all 140
sampled snapshots through frame 1,400 match the ROM reference byte-for-byte, including stack
WRAM. The default 18-function set executes 11,794 native interceptions and matches all observable
state when the documented dead stack-scratch range `$1F00-$1FFF` is excluded.

September coverage expansion adds four generated roots: `$81F722`, `$81FD22`, `$8087D9`,
and `$80879A`. Together they execute 9,220 interceptions and pass all 140 raw snapshots.
The opt-in `SMK_RECOMP_INTERCEPTS=coverage` preset combines them with the legacy 18 roots:
21,014 interceptions and 140/140 snapshots matching outside the legacy dead stack range.
Use `SMK_INTERP=0 SMK_RECOMP_BUSPHASE=1 SMK_RECOMP_PHASE_YIELD=1` with `SMK_RECOMP=1`.
See [research, reproduction and next milestones](related_projects_and_coverage_plan.md).

An additional semantic reconstruction of `$81BB70` is available as `smk_mix_word`.
Its 65,536-input, two-flag-pattern test executes the supplied ROM with LakeSnes's CPU
and verifies returned/stored values and CPU flags. The generated timed body remains
the game implementation; this adds a readable algorithm without changing entry counts.
See [binary derivation and scope](binary_word_mix.md).

## What this branch does not claim

- It is not a complete C recompile of the game.
- It does not yet replace the LakeSnes-owned reset/NMI/main-frame scheduler.
- Bus-phase timing remains opt-in. Generated immediate, implied, branch, data-access, RMW,
  stack, JSR/JSL, RTS, and RTL paths now place `checkInt` at their LakeSnes micro-phase. MVN/MVP
  and RTI now also have ordered micro-phase implementations. They do not occur in the current 37
  generated functions, however, so ROM-level closure coverage is still missing. Generated direct
  page, absolute-indexed, direct-page-indirect, and stack-relative modes now apply their dynamic
  LakeSnes addressing idles in bus order. The generated `$80:946E` OAM-DMA leaf preserves the
  exercised deferred DMA stall and passes a 1,071-hit, 140/140 raw-snapshot gate; bus-phase
  accesses inherit LakeSnes open-bus updates. The legacy aggregate path remains approximate and
  internal bus-latch state is not directly snapshot-gated. The `$81FD22 -> $81F638` JSL child
  closure is now ROM-gated; general indirect JMP/JML closures still need equivalent validation.
- Passing the standard route does not prove every game mode, character, cup, track, multiplayer
  path, or long-running race.
- Interpreter fallback is stack-safe but intentionally untimed; it is excluded from exact-closure
  claims.

See [`recompilation_roadmap.md`](recompilation_roadmap.md) for milestone gates M1-M6 and
[`own_recompiler_design.md`](own_recompiler_design.md) for implementation notes.

## Repository and submodule layout

This branch points `ext/snesrecomp` at the
[`Zaxaerith/snesrecomp`](https://github.com/Zaxaerith/snesrecomp) fork because the call-frame and
timing-yield runtime changes must be available before the parent project can be checked out
reproducibly. Clone with submodules:

```bash
git clone --branch contrib/recompiler-correctness --recurse-submodules \
  https://github.com/Zaxaerith/mk.git
cd mk
git submodule update --init --recursive
```

## ROM and legal boundary

No game ROM, firmware, save-state, validation snapshot, trace, screenshot dump, executable, or
other extracted game asset is included by this contribution. `.sfc`, `.smc`, firmware, runtime
state, build directories, and diagnostic outputs are ignored by Git.

To run or validate the project, supply your own legally obtained US v1.0 ROM. The expected MD5 is
`7f25ce5a283d902694c52fb1152fa61a`. Keep it at the repository root as
`Super Mario Kart (USA).sfc`, or pass an explicit path to the launcher.

## Build and unit test

Requirements are Visual Studio 2022, CMake 3.16+, Python 3.10+, and vcpkg. SDL2 is declared in
`vcpkg.json`.

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --target smk_launcher
python -m unittest tools.recomp.test_autogen -v
```

The launcher searches its own directory and up to two parent directories for the ROM, so the
Release executable may be launched directly while the ROM remains at the repository root.

## Deterministic validation route

The standard controller script is:

```text
360:Y,360:START,420:START,480:START,540:START,600:START,
660:START,720:START,780:START,840:START,900:START,960:START,
1120-1399:B
```

Generate a reference snapshot set with real-frame ROM execution, then run the exact C closure
with these additional environment variables:

```text
SMK_HEADLESS=1
SMK_MAX_FRAMES=1400
SMK_SNAPSHOT_EVERY=10
SMK_INTERP=0
SMK_RECOMP=1
SMK_RECOMP_INTERCEPTS=81F638:L
SMK_RECOMP_BUSPHASE=1
SMK_RECOMP_PHASE_YIELD=1
```

Use a different `SMK_SNAPSHOT_PREFIX` for the reference and test runs, then compare them:

```bash
python tools/diff_snapshots.py path/to/reference/snap path/to/test/snap
```

The acceptance result for this branch is `No divergence across all compared frames` for 140
frames `[10..1400]`. For the default 18-function aggregate set, use
`--ignore-wram 1F00-1FFF`; that range contains stale return-address scratch rather than live game
state.

## Publication checks

M3g was published at parent `5665b45` / submodule `78245a6`. The September coverage expansion
and semantic reconstruction are prepared for publication. The previous audit covered the first five items below;
before any future push, repeat the audit and publish the submodule commit before its parent gitlink:

- the ROM is ignored and absent from the Git index;
- build products, snapshots, traces, save-states, and local configuration are absent;
- no absolute local filesystem paths, credentials, access tokens, or private keys are present in
  the branch diff;
- the generated source is reproducible from the checked-in generator/profile;
- the 35 Python generator tests and the Visual Studio Release build pass;
- the parent gitlink must resolve to a published `snesrecomp` contribution-branch commit.
