# Conditional fixed-point extraction ($80:86A0)

Recovered 2026-09-19 as `smk_project_coordinates` in the semantic C library.

The ROM first reads DP-indexed words at `$A4,X` and, unless that word equals
2, `$AC,X`. It returns without updating destinations unless the first equals
2 or the second has bit 15 set. The names `mode` and `flags` describe these
operations; their full gameplay meanings remain to be established.

The taken path reads two little-endian 32-bit source values from
`DB:$0016,Y` and `DB:$001A,Y`. It alternates ROL operations on each low/high
word twice and writes the resulting high words to DP `$88,X` and `$8C,X`.
The destination is exactly `(uint16_t)(source >> 14)`: initial carry only
enters discarded low bits, while the source low word's top two bits enter
the destination. This extracts bits 14..29 without signed-shift ambiguity.

The pure C API leaves both output pointers untouched when its condition is
false. On the taken path pointers must be valid and distinct. It deliberately
does not claim CPU scratch/carry/register equivalence, source/destination
alias behavior, or interrupt/MMIO timing. It is not installed as a timed
replacement for the existing registered routine.

## Differential evidence

```sh
build-manifest/Release/test_projection.exe "Super Mario Kart (USA).sfc"
```

The test executes supplied ROM bytes in LakeSnes's CPU, with independent
RAM callbacks for input, scratch, output and stack. It checks both result
words, zero destination writes on skipped paths versus four byte writes on
taken paths, preserved X/Y/DP/DB/SP/width state, and bounded completion.

**4,194,304 cases pass**: every high 16-bit word, eight low-word boundary
patterns and eight guard combinations. The second source is distinct and
varies concurrently. Unexpected memory accesses fail. This is not exhaustive
over all pairs of 32-bit sources or all memory alias arrangements.
