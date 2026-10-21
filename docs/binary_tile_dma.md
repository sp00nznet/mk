# Sprite Tile DMA Staging Queue Append ($80:8D83)

Recovered 2026-09-19 as `smk_queue_tile_dma` in the semantic C algorithm library.

## What the binary establishes

The routine implements the sprite tile DMA staging queue append operation called
1,191 times during the standard 1,400-frame title-to-race route.

Entry assumes native 16-bit mode (M=X=0) and binary arithmetic (D=0).
Input state:
- Direct page word at `$2C`: general stage/draw state guard. Non-zero aborts immediately without modifying state.
- Current queue index word at RAM `$012E` ($X$) compared against queue limit word at RAM `$0144`. If $X \ge \text{limit}$ (unsigned), aborts immediately without modifying state.
- Staging buffer write position $Y$ at direct page `$4A`.

On the taken path:
1. Loads VRAM base destination word from ROM table at `$80:8DD1 + X`.
2. Loads tile ID word from RAM `$0130 + X`.
3. Selects ROM tile source word:
   - If $X < 8$: loads word from ROM table `$80:8DE1 + \text{tile\_id}`.
   - If $X \ge 8$: loads word from ROM table `$80:8DF1 + \text{tile\_id}`.
4. Writes two 6-byte DMA staging descriptors (total 12 bytes / `$000C`) to RAM `$0EA0 + Y`:
   - Descriptor 1:
     - `+0`: VRAM destination address
     - `+2`: ROM source address
     - `+4`: `$407F` (128-byte DMA staging parameter / transfer config)
   - Descriptor 2:
     - `+6`: `(VRAM destination + 0x0100)` (subsequent tile row in VRAM)
     - `+8`: `(ROM source + 0x0200)`
     - `+10`: `$407F`
5. Increments queue state:
   - Advances buffer position: DP `$4A` $\leftarrow Y + 12$.
   - Increments queue index: RAM `$012E` $\leftarrow X + 2$ (`INC $012E` executed twice in 16-bit mode).

Returns `true` when descriptors are queued, `false` when guards fail.

## Differential ROM validation

```sh
build-manifest/Release/test_tile_dma.exe "Super Mario Kart (USA).sfc"
```

The differential test loads the user's legally dumped ROM at runtime and invokes LakeSnes's
65C816 CPU directly on the `$80:8D83` ROM instructions.

The memory harness validates:
- Zero data RAM writes on skipped branches (`$2C \ne 0` or $X \ge \text{limit}$).
- Exactly 18 byte writes on taken branches (12 descriptor bytes, 2 bytes for DP `$4A`, 4 bytes for 2× 16-bit `INC $012E`).
- Byte-for-byte matching of all descriptor fields against `smk_queue_tile_dma`.
- Preserved CPU width, stack pointer, direct page, data bank and execution within 45 opcodes.

**96,768 cases pass**: covers 8 guards (including 0, positive, negative, and 16-bit limits),
all 9 queue limits (0..16), 14 queue index values, 12 buffer positions, and all 8 tile IDs.


## Scope and integration boundary

The pure C algorithm reconstructs the data updates and queue state. The timed generated
routine `smk_808D83` remains in the game dispatch table to supply micro-phase bus timing,
memory access sequencing, and interrupt checkpoints.
