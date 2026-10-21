# Adjacent object-order repair ($80:A027)

Recovered 2026-09-19 as `smk_repair_object_order`. The root examines the
current/previous handles at `DB:$010E,Y` / `DB:$010C,Y`. Zero Y, a zero
handle, current key <= previous key, or predecessor flag `$20` stops it.
Keys live at DP `$C0,handle`, flags at DP `$10,handle`.

Otherwise it swaps the two order words, updates both objects' reverse slot
offsets at DP `$E6,handle`, subtracts two from Y and repeats. The recovered
API normalizes original object byte offsets into caller-owned object indices;
zero stays a sentinel. Reverse slots remain *byte* offsets. This is a local
descending insertion-order repair with a protected-predecessor condition,
not a general full sort. Equal keys never exchange. The specific gameplay
meaning of the keys/protection flag is still not established.

`tools/test_object_order.c` runs actual supplied ROM instructions and compares
all data RAM, excluding dead stack scratch, to the pure algorithm's updates.
It also checks the resulting Y offset and preserved return/width state.

```sh
build-manifest/Release/test_object_order.exe "Super Mario Kart (USA).sfc"
```

**811,440 cases pass**: every permutation of seven handles, every starting
slot, four key distributions (ascending, descending, equal, wrapped unsigned),
four flag patterns, plus each zero-sentinel position on all permutations and
starts. This covers unsigned key ordering, equality, protected predecessors,
reverse-reference updates and unchanged unrelated fields. It does not cover
all possible 16-bit keys, object alias arrangements or CPU flag/scratch/timing
effects. The pure API requires valid handles and distinct nonoverlapping data;
the original timed routine remains in use.
