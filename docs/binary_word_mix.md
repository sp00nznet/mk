# Recovering a compact C algorithm from ROM $81:BB70

2026-09-12. This is an independently derived semantic implementation of the
user-supplied US ROM routine, not a claim to have recovered its original source
text, variable names or author intent.

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

## Verification against actual ROM instructions

`tools/test_word_mix.c` loads the supplied ROM at runtime and invokes LakeSnes's
65C816 CPU on the original `$81:BB70` bytes. Its isolated memory callbacks accept
only the expected code, RAM and return-stack accesses. Every input word is run
with two initial flag patterns, including decimal/overflow and carry differences.

The gate compares A, the stored word, final N/Z/C, preserved flags/registers,
return stack position and completion at an RTL sentinel. It requires exactly
four data-byte writes (the intermediate and final words), rejects unexpected
memory accesses and bounds execution to 32 instructions.

```sh
cmake --build build-manifest --config Release
build-manifest/Release/test_word_mix.exe "Super Mario Kart (USA).sfc"
```

Result: **131,072 executions pass**, covering all 65,536 input words twice.
There is one input whose stored state differs from its returned value. The first
draft incorrectly folded the mixed byte rather than the original low byte; the
ROM gate caught this at input `0x0100`, and the implementation was corrected.

This test compares against CPU execution of the ROM, not a second manually
transcribed algorithm or output from the C generator. It checks functional
semantics with interrupts disabled. It does not prove bus timing, interrupt
resumption, MMIO aliases, or behavior when external agents mutate the state
during the routine.

## Integration boundary

The recovered algorithm is compiled into the game library and the exhaustive
test target. It is not substituted for the registered timed `smk_81BB70` body:
the generated implementation still supplies instruction fetches, intermediate
memory writes and interrupt suspension points in game execution. A future
semantic-runtime adapter must preserve those effects before replacing that path.

This increment adds one readable, exhaustively checked algorithm, not another
registered ROM entry: the totals remain 85 registered entries and 37 generated
functions. No ROM, extracted binary or reference output is needed in Git.
