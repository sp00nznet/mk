# Recovered SMK direction semantics

These names describe independently recovered calculations, not original
Nintendo source symbols. The implementation is migrated from my earlier
Zaxaerith/mk commits `e30516b4a5518f15bfb97f8e860ed9063ac4d389` and
`7cb1fff4ebdaeb2bf76782036f13c97753c6aeed`, preserved in
`audit/historical-pr1`. It is submitted under this repository's MIT license.
No GPL/non-commercial emulator implementation was used.

The public API is `smk/direction.h`; all lookup bytes are caller-supplied.
No ROM table, disassembly listing or generated ROM C is distributed here.
These pure functions do not register interceptions, replace ROM execution,
change the default game path or provide bus/interrupt timing.

## Evidence and scope

The historical instruction analysis identified the operations below. The
bounded CPU gate now checks their observable results using actual instructions
from a user-supplied US v1.0 ROM, not a translated C reference. Descriptions
such as grid dimensions are mathematical inferences from those operations.
Gameplay roles such as AI, collision, navigation or pathfinding remain
unconfirmed; they require separate caller analysis.

ROM comparison assumes native mode, 16-bit A/X/Y, decimal clear, direct page
zero, stable ordinary RAM and the specified tables. The isolated adapter sets
DB=$7E for F638/FD22 and DB=$00 for F722. The C API returns only the documented
results, not all CPU scratch registers, flags, MMIO writes or interrupts.

## $81:F638: signed vector to quantized 16-bit direction

`smk_vector_direction(x, y, table)` accepts signed 16-bit components and at
least 4097 stable bytes corresponding to RAM starting at $7F:8FFF. Its result
contains an angle and `undefined`, the original carry result.

Instruction-confirmed special paths:

- X=0: Y<0 returns $0000, Y>0 returns $8000.
- X nonzero and Y=0: X>0 returns $4000, X<0 returns $C000.
- X=Y=0: angle $0000 and carry/undefined true.
- Every nonzero vector returns carry/undefined false.

For non-axis vectors, compare the unsigned absolute magnitudes **before**
shifting. X-major means abs(X)>abs(Y); equality selects Y-major. Both
magnitudes are shifted right together until each is at most 63. The byte
index is `minor * 64 + major`, in [0,4095]. Read a little-endian word at that
index and its successor; byte 4096 is therefore required. Widen before
negating in C so abs(-32768) becomes 32768 without signed overflow.

Let W be that word. The instruction sequences imply the following modular
16-bit expressions; retain only the high byte of each result:

| Major axis | Signs (X,Y) | Expression before final high-byte mask |
|---|---|---|
| X | +,+ | $4000 + W |
| X | +,- | $40FF - W |
| X | -,+ | $C0FF - W |
| X | -,- | $C000 + W |
| Y | +,+ | $80FF - W |
| Y | +,- | W |
| Y | -,+ | $8000 + W |
| Y | -,- | -(W & $FF00) |

The final row masks **before** negation. For W=$12FF it returns $EE00,
whereas negating the entire word then masking would return $ED00. Tests vary
that low byte explicitly. Axis results do not consult the table. Arbitrary
synthetic tables need not describe geometric angles; they test the recovered
integer operations without embedding game data. No atan2/floating point is used.

## $81:FD22: direction from a packed cell

`smk_direction_from_cell(cell, point_x, point_y, table)` follows the recovered
caller calculation:

```text
center_x = (cell & 63) * 16 + 8
center_y = ((cell >> 6) & 63) * 16 + 8
delta_x  = signed16(wrap16(point_x - center_x))
delta_y  = signed16(wrap16(point_y - center_y))
direction = vector_direction(delta_x, delta_y, table)
```

The high four cell bits do not contribute. A 64x64 grid, 16-unit cell size
and center offset 8 follow from this arithmetic; no gameplay role is claimed.
The original reads positions at DB:$0900,Y and DB:$0A00,Y. The API accepts
the two position values directly and returns both signed deltas. Wrap occurs
before signed interpretation, including differences equal to $8000. The CPU
gate fixes Y=0 at entry; it checks the deltas read from CPU scratch $12/$14
because those two values are explicitly returned by this API.

## $81:F722: 8-bit direction and divider quantization

`smk_vector_direction8(x, y, table)` returns only the low A byte and uses
256 stable caller-supplied bytes corresponding to $7F:0000. Its positive-Y
axis is $00 and negative-Y axis $80, unlike F638. X axes are $40/$C0;
the zero vector returns $00.

For non-axis vectors, Y-major means abs(Y)>abs(X); equality is X-major here.
Shift major/minor until major<=256. Each discarded minor bit rotates into a
16-bit fraction word. The overlapping word load at DP $09 implies:

```text
dividend = wrap16((minor << 8) | (fraction >> 8))
divisor  = low8(major)
index    = min(hardware_unsigned_quotient(dividend, divisor), 255)
```

Major=256 yields divisor zero: the hardware result is $FFFF, clamped to
index 255. Equal magnitudes can also produce quotient 256, which is clamped;
minor=256 additionally truncates the dividend. A floating-point ratio is not
equivalent. For Y-major, reflect the sample when signs differ and bias by
$00/$80 according to Y's sign. For X-major, reflect when signs agree and bias
by $40/$C0 according to X's sign. Addition/negation wraps modulo 256.

The original changes M/X on return and touches divider/scratch registers.
Those effects, A's high byte, hardware latency and interrupts are not part of
the pure API. The CPU gate routes $4204..$4207 writes and $4214/$4215 reads
to the pinned MIT LakeSnes implementation. It does not duplicate division in
the reference adapter or advance/assert hardware master-clock latency.

## Reproducible lightweight checks

ROM-free strict C17 check (any temporary output may be used):

```sh
gcc -std=c17 -Wall -Wextra -Werror -pedantic -O2 -Iinclude tools/test_direction_unit.c src/recomp/smk_direction.c -o build/test_direction_unit
build/test_direction_unit
```

Optional CMake integration, using the current root project's SDL2 and pinned
submodules (the standalone unit check above needs none of those dependencies):

```sh
cmake -S . -B build -DSMK_BUILD_DIRECTION_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target test_direction_unit test_vector_direction smk_game
ctest --test-dir build -R '^direction_unit$' --output-on-failure
```

The oracle expects the US v1.0 ROM: payload MD5
`7f25ce5a283d902694c52fb1152fa61a`. Verify identity before invocation; the
harness checks size only (512 KiB with optional 512-byte copier header).
It never finds or downloads a ROM. The default and `--sample` modes are
bounded. Only explicit `--exhaustive` enables the historical large sweeps:

```sh
build/test_vector_direction --sample "Super Mario Kart (USA).sfc"
```

Do not run exhaustive mode as routine CI. The whitelist adapter permits only
routine code, stack, scratch, position fields, synthetic tables and divider
registers. Each invocation has a 160/210/180-opcode limit respectively and
must reach its RTS/RTL sentinel. Unexpected access, nontermination or result
difference fails immediately at that case. Only API results and the harness
return-stack protocol are compared, not every CPU register or scratch byte.

## Historical evidence versus current execution

Historical documentation dated 2026-09-19 reported **6,489,603 vector**,
**1,572,864 packed-cell** and **6,489,603 byte-direction** executions: a dense
[-128,128] square, 16 full-domain edge sweeps in both orientations, and all
65,536 cell words with eight position patterns, across three synthetic tables.
Those are historical results, **not rerun in this migration**. They are not an
exhaustion of all 2^32 vectors, all position pairs or arbitrary lookup tables.

Current local validation (2026-10-26, Windows GCC 16.1.0):

- Strict standalone C17: **655 fixed-expectation checks pass**. Includes axes,
  all octants, equality, compare-before-shift, -32768, three synthetic tables,
  low-byte masking, endianness/final table byte, all cell high nibbles, wrapped
  deltas, fraction retention, zero divisors and clamped index 255.
- CMake Release: the two new test targets and smk_game build; CTest **1/1**
  passes. Existing dependency artifacts are reused, with no toolchain rebuild.
- Actual ROM `--sample`: **1,728 vector + 288 cell + 1,728 byte = 3,744**
  executions pass across zero, FF and position-dependent synthetic tables.
  The 24x24 vector sample includes magnitude/shift boundaries; 12 cell words
  each use eight position patterns. Observed **1,587 divider writes, 723 zero
  divisors and 867 byte-table index-255 reads** confirm special path execution.
- Runtime pin `b73fdcd1322103f15fa33401889cd7b94eb9e79b`, LakeSnes pin
  `9f1de1e9b7064f496bdc659c40435caa27c37a52`, SDL2 2.32.8; ROM identity
  matches the MD5 above. Full oracle source also passes strict C17 syntax checking.

No full-game rebuild, title-to-race regression, live interception validation,
MMIO latency check, cross-compiler check or exhaustive sweep was performed.
Title-to-race validation remains a pre-publication Phase 1.5 check under
CONTRIBUTING.md. Pure calculations passing an isolated CPU sample do not prove
all-state/gameplay equivalence or that these APIs replace the running game.
