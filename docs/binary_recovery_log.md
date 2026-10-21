# Ongoing binary recovery

Goal started 2026-09-19: continue recovering readable C algorithms from the
locally supplied ROM, verify against actual CPU execution, and save incremental
local checkpoints until the user stops work or usage/runtime limits intervene.
This is an ongoing goal; the entries below are checkpoints, not completion of
the full recompilation project.

## Checkpoint 1 — vector direction and packed-cell direction

Added `smk_vector_direction` ($81:F638 plus eight child branches) and
`smk_direction_from_cell` ($81:FD22) to the semantic algorithm library. Their
ROM differential gate passes 6,489,603 and 1,572,864 cases respectively.
Existing word-mixer and generator regressions pass. See `binary_direction.md`
for evidence, preconditions and limitations. No timed game routine was replaced.

## Checkpoint 2 — hardware-divider byte direction

Recovered $81:F722 as `smk_vector_direction8`. The ROM CPU gate routes divider
accesses into LakeSnes's actual register implementation and passes 6,489,603
cases; vector and cell gates remain green. The zero-divisor boundary at a
reduced magnitude of 256, overlapping scratch-word read and differing axis
convention are preserved. See `binary_direction.md` for the timing/API limits.

## Checkpoint 3 — conditional coordinate projection

Recovered $80:86A0 as `smk_project_coordinates`; all 4,194,304 ROM tests pass,
including skipped paths with no destination writes. See `binary_projection.md`.
Its ROL sequence simplifies to extracting bits 14..29.

## Checkpoint 4 — adjacent object-order repair

Recovered $80:A027 as `smk_repair_object_order`: 811,440 ROM cases pass across
all seven-object permutations, guard/key patterns and holes. See `binary_object_order.md`.

## Checkpoint 5 — live race-path direction semantic gate

Connected `smk_vector_direction` and `smk_vector_direction8` to optional read-only
live assertions at generated routine entry/normal return (`smk_semantic_begin` /
`smk_semantic_end`). The deterministic 1,400-frame title-to-race route passes with
zero mismatches across 2,670 calls to $81:F638 and 4,096 calls to $81:F722; all 140
raw snapshots match LakeSnes oracle state outside dead stack scratch. See
`tools/recomp/validate_route.py --semantic-check`.

## Checkpoint 6 — sprite tile DMA staging queue append

Recovered $80:8D83 as `smk_queue_tile_dma`: the staging queue builder called 1,191
times during the standard route. 96,768 ROM CPU test cases pass across 8 guard
patterns, all 9 queue limits, 14 queue positions, 12 buffer positions, and all 8 tile
IDs. Validates double guards ($2C and queue limit $0144), ROM table lookups,
12-byte queue descriptors and pointer advances. See `binary_tile_dma.md`.


## Next investigation

Connect `smk_repair_object_order` and `smk_queue_tile_dma` to live assertion checks,
or continue recovering callers ($80:A01F) and adjacent object physics routines.
Keep data tables caller-supplied and verify against ROM CPU execution.


