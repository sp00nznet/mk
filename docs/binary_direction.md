# Direction algorithms recovered from ROM

Checkpoint: 2026-09-19. These functions reconstruct observable calculations;
names describe the recovered mathematics, not original source symbols.

## $81:F638: signed vector to quantized direction

`smk_vector_direction(x, y, table)` implements the A/carry result of the root
and its eight computed-JSR callees ($F6C7 through $F710). Preconditions are
native mode, 16-bit A/X/Y and binary arithmetic (D clear). The 4097-byte table
parameter represents runtime RAM beginning at `$7F:8FFF`. It is not included
in the repository.

Zero X is handled before any magnitude calculation: negative Y returns zero,
positive Y returns `$8000`; the zero vector returns zero with carry set. With
nonzero X and zero Y, positive X returns `$4000`, negative X `$C000`. Every
nonzero vector returns carry clear.

Otherwise signs select an octant. Compare absolute magnitudes *before* shifting
them; equality selects the Y-major branch. Shift both right until the larger is
at most 63. The byte index is `64 * minor + major`. Read a little-endian word
there, apply the octant's add/subtract/reflection and retain its high byte.
All intermediate arithmetic wraps like the original 16-bit CPU. Negation is
widened in C to correctly handle -32768.

The Y-major negative-X/negative-Y case is subtly different: mask the table word
to its high byte *before* negating. This matters for arbitrary table low bytes.
The pure implementation preserves that sequence.

## $81:FD22: direction from packed grid-cell center

`smk_direction_from_cell(cell, point_x, point_y, table)` recovers the caller:

```
center_x = (cell & 63) * 16 + 8
center_y = ((cell >> 6) & 63) * 16 + 8
delta_x  = signed16(point_x - center_x)
delta_y  = signed16(point_y - center_y)
direction = vector_direction(delta_x, delta_y, table)
```

The original loads the position from `DB:$0900,Y` and `DB:$0A00,Y`. The pure
API accepts those values directly, returns both deltas, and preserves 16-bit
wrapping before interpreting their signs. The packed cell's high four bits
do not affect the calculation. Calling these coordinates a 64x64 grid with
16-unit cells is an inference from the bit fields and center offsets; a specific
gameplay use (AI, collision, navigation) still requires caller analysis.

## Direct ROM differential gate

Build Release and execute:

```sh
build-manifest/Release/test_vector_direction.exe "Super Mario Kart (USA).sfc"
```

The test executes actual ROM instructions with LakeSnes's CPU. The memory
adapter supplies only routine code, stack, scratch words, position values and
the table. Unexpected accesses and nontermination fail. No translated C routine
is used as the reference.

Three synthetic table patterns (zero, all ones, and position-dependent bytes)
exercise the formulas without embedding game data. Results:

- **6,489,603 vector cases**: dense [-128,128] square plus 16 boundary values
  swept against the entire signed 16-bit domain in both orientations, for all
  three table patterns.
- **1,572,864 cell cases**: all 65,536 packed words, eight position patterns
  including cell centers and wrapping boundaries, for all three table patterns.
- Existing word mixer: **131,072 cases** still pass; 35 generator tests and
  Release build pass.

Vector tests compare A/carry and preserved Y/DB/DP/SP/status-width state. Cell
tests additionally check scratch deltas and preserved packed X. They do not
assert all scratch/register/flag side effects of the vector routine, and do
not exhaust the full 2^32 vector space or every possible table. Decimal mode,
interrupts, bus timing and concurrent table mutation are outside these APIs.

The game still uses the timed generated routines. These pure algorithms are
compiled into the library and the test executable, but do not replace timed
interceptions. Registered/generated entry counts therefore remain 85/37.

## $81:F722: byte direction with hardware-divider quantization

`smk_vector_direction8` returns the low accumulator byte for another direction
routine. Its positive-Y axis is zero (negative Y is `$80`), unlike F638; the
X axes remain `$40` and `$C0`. It also returns zero for a zero vector.

The general path sorts absolute magnitudes, records sign and major-axis bits,
then shifts until the larger magnitude is at most 256. Each discarded minor
bit is rotated into a fraction word. The overlapping word read at DP `$09`
forms `(minor << 8) | (fraction >> 8)`, truncated to 16 bits. The hardware
divides this by the low byte of the major magnitude. Consequently exactly 256
means a zero divisor, producing `$FFFF`, then clamped to index 255. Equal
magnitudes at 256 also truncate the numerator; do not simplify this to a
floating-point ratio. A 256-byte caller-supplied table and octant reflection/
bias produce the result modulo 256.

The test now additionally executes **6,489,603 byte-direction ROM cases** on
the same grids/sweeps and three 256-byte table patterns. ROM CPU accesses to
divider registers are routed to LakeSnes `snes_read`/`snes_write`, rather than
duplicating division in the reference adapter. This validates register-result
semantics, not hardware divider latency: no emulated master-clock progression
is asserted by this isolated gate. The original returns with M/X set to 8-bit;
the API returns only the angle and does not reproduce that status change, the
accumulator's high byte, scratch values, or hardware register side effects.
