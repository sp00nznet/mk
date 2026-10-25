# Recovering a compact C algorithm from ROM $81:BB70

Originally recovered on 2026-09-12; migrated from commit
`d28b5355c09a251e61cb5dc037f75992b40c1a09` of Zaxaerith/mk (PR #1).
This is an independently derived semantic implementation of the user-supplied
US ROM routine, not Nintendo's original source text, symbols or author intent.
The historical gate used real CPU execution as a black-box reference. No code
from GPL/non-commercial emulators or the mstan-based project was imported.

## What the binary establishes

The routine reads a word at `DB:$1F26`, transforms it using byte swaps, XOR and
shifts, stores the result there, and returns via RTL. Entry assumes native mode
with a 16-bit accumulator and ordinary, stable RAM behind that address.

Let the original high and low bytes be `H` and `L`. The sequence through `$BB7B`
stores `T = (L << 8) | (H ^ L)`. The additional byte swap and mask recover `L`,
so the next stage is `F = T ^ (L << 1)`, followed by
`V = (F >> 1) ^ 0xFF80`.

- If bit zero of `F` was set, XOR `V` with `0x8180` and store/return it. Carry
  remains set from LSR; N and Z describe the final value.
- Otherwise compare `V` with `0xAA55`. Store `V` unless equal, in which case
  store zero. Return `V` in either case. N/Z/C describe the comparison, not
  necessarily the returned accumulator or stored word.

That last distinction matters: replacing this routine with a function returning
only a next state would lose observable CPU behavior. The pure C implementation
therefore returns separate `state`, `value`, `carry`, `zero`, and `negative` fields.
Other status bits and X/Y/DP/DB are unchanged.

The stateful bit feedback suggests a pseudorandom-state routine, but that
gameplay role remains a hypothesis pending caller analysis. The API uses the
neutral name `smk_mix_word` rather than asserting a particular RNG design.

Implementation: `src/recomp/smk_algorithms.c`, public interface:
`include/smk/algorithms.h`.

## Small standalone checks (no ROM or runtime)

`tools/test_word_mix_unit.c` contains 12 hand-calculated mathematical examples,
not extracted ROM results. It checks every result field, both feedback branches,
byte/sign boundaries, zero input, a zero feedback result and the comparison
threshold. Inputs `0x52AA`, `0x54AA` and `0x56AA` return `0xAA56`, `0xAA55` and
`0xAA54` respectively. At `0x54AA` the stored state is zero while A is `0xAA55`;
Z comes from CMP. At input zero, returned A has its sign bit set but final N is
clear, so deriving N from A would also be wrong.

For example, with GCC in PowerShell at the repository root:

```powershell
New-Item -ItemType Directory -Force build/m1 | Out-Null
gcc -std=c17 -Wall -Wextra -Werror -pedantic -O2 -Iinclude tools/test_word_mix_unit.c src/recomp/smk_algorithms.c -o build/m1/test_word_mix_unit.exe
./build/m1/test_word_mix_unit.exe
```

With the existing project dependencies available, configure the normal build
with `-DSMK_BUILD_SEMANTIC_TESTS=ON`, then build only `test_word_mix_unit` or
`test_word_mix`. The option defaults to OFF; CTest registers only the ROM-free
`word_mix_unit` check, never the ROM oracle or the exhaustive run.

## Bounded CPU oracle and optional exhaustive gate

`tools/test_word_mix.c` loads the supplied ROM at runtime and invokes LakeSnes's
65C816 CPU on the original `$81:BB70` bytes. Its isolated memory callbacks accept
only the expected code, RAM and return-stack accesses. By default it runs the
same 12 selected inputs with two initial flag patterns (24 ROM executions),
including decimal/overflow and carry differences. `--sample` selects this mode
explicitly; only `--exhaustive` runs all 65,536 input words twice.

The gate compares A, the stored word, final N/Z/C, preserved flags/registers,
return stack position and completion at an RTL sentinel. It requires exactly
four data-byte writes (the intermediate and final words), rejects unexpected
memory accesses and bounds execution to 32 instructions.

```powershell
cmake --build build --config Release --target test_word_mix
./build/Release/test_word_mix.exe --sample "<path-to-your-US-ROM>"
# Optional historical-sized gate, not required for a bounded check:
./build/Release/test_word_mix.exe --exhaustive "<path-to-your-US-ROM>"
```

These paths assume a multi-configuration build; a single-configuration build
places the executables directly in `build/`. The CPU gate needs LakeSnes, and
normal project configuration also needs SDL2 and the pinned submodules. Supply
your own US v1.0 ROM (upstream's documented MD5:
`7f25ce5a283d902694c52fb1152fa61a`). The harness accepts a 512 KiB file with an
optional 512-byte copier header; it does not verify the ROM hash or auto-detect
other revisions/mappings. Never commit ROMs or test artifacts.

Historical result (2026-09-12): **131,072 executions were reported passing**,
covering all input words twice, with one stored/A difference. This was recorded
with snesrecomp `78245a6a3dfac087e4237a89c7e3006c5d9bedfc` and LakeSnes
`7df883dc778cd7344af6dec7eae370858f97caca`; it is not a result for the current
upstream combination. The first draft folded the mixed byte instead of the
original low byte; the historical CPU gate caught that at input `0x0100`.

Local validation on 2026-10-25 used Windows, GCC/G++ 16.1.0, CMake 4.4.4 and
SDL2 2.32.8. The strict C17 standalone build passed all 12 ROM-free examples.
The Release CMake build of both tests and the launcher succeeded, and the
ROM-free CTest passed (1/1). The CPU oracle's `--sample` mode passed 24 actual
ROM executions, including one input whose stored state differs from A.
One bounded 1,400-frame headless title-to-race run reached race state at frame
1,095 and exited normally; title and ROUND 1 captures were checked locally.
These checks used the US v1.0 ROM identity above, snesrecomp
`b73fdcd1322103f15fa33401889cd7b94eb9e79b` and LakeSnes
`9f1de1e9b7064f496bdc659c40435caa27c37a52`. The historical 131,072-execution
exhaustive gate was not rerun during this migration.

This test compares against CPU execution of the ROM, not a second manually
transcribed algorithm or output from the C generator. It checks functional
semantics with interrupts disabled. It does not prove bus timing, interrupt
resumption, MMIO aliases, or behavior when external agents mutate the state
during the routine.

## Integration boundary

The recovered algorithm is compiled into the game library and, when requested,
the standalone tests. It is not registered in the dispatch table or substituted
for an instruction-level `smk_81BB70` body. The function returns data only; it
does not touch g_cpu or RAM and does not reproduce intermediate writes. A future
runtime adapter would need to preserve those effects and timing separately.

This increment adds one readable algorithm and focused validation. It does not
change the launcher, interception defaults, runtime pin or optional autogen build.
No ROM, generated C, disassembly, extracted binary or reference dump is included.
