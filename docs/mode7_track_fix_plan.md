# Mode-7 Garbled Track — Diagnosis & Fix Plan

_Last updated: 2026-06-01_

## Symptom
In the attract-demo Mode-7 race (and gameplay), the road plane renders as a
**garbled red/blue perspective field** instead of the real track surface.
Sprites (karts), HUD, mountains, sun, and the Mode-7 *perspective shape* all
render correctly — only the BG1 Mode-7 **texture/colors** are wrong.

## Key finding: this is a reference-side (emulation) bug, NOT a recomp bug
The attract demo runs the **real ROM via the LakeSnes interpreter** (force mode,
the default). It still renders garbled. Therefore the lockstep
recompiled-vs-interpreter harness (`tools/diff_snapshots.py`) **cannot** fix
this one — the reference itself is wrong. The fault is in snesrecomp's PPU /
frame / DMA path, downstream of which the recompiled code is irrelevant.

(The harness remains the correct tool for the *other* Mode-7 issue — the
recompiled path not setting up Mode-7 at all, e.g. `$A4` divergence. That is a
separate track. See `snapshot_diff_harness` in memory.)

## Why the renderer is exonerated
`ppu_getPixelForMode7` in the LakeSnes fork
(`ext/snesrecomp/ext/LakeSnes/snes/ppu.c:545`) is textbook-correct:
```c
uint8_t tile  = ppu->vram[(yPos>>3)*128 + (xPos>>3)] & 0xff; // map  = LOW byte
uint8_t pixel = ppu->vram[tile*64 + (yPos&7)*8 + (xPos&7)] >> 8; // char = HIGH byte
```
8bpp chunky pixels (no bitplane decode), correct map/char byte split. The same
code renders Mode-7 correctly in snesrev's zelda3/smw. And the perspective is
correct here, so the matrix + per-scanline HDMA are working.

=> If the renderer and matrix are right, the **VRAM contents** (Mode-7 char data
in the HIGH bytes of words `$0000-$3FFF`) and/or **CGRAM palette** must be wrong.

## Ranked hypotheses
1. **VRAM Mode-7 char data wrong** (most likely). snesrecomp uses a *custom*
   frame model — it does NOT call LakeSnes' native `snes_runFrame`; it manually
   runs PPU lines and added "untimed HDMA in end_frame". The large Mode-7
   graphics upload (DMA during forced-blank when entering a race) may be
   mis-ordered, mis-incremented (VMAIN `$2115`), or routed to the wrong VRAM
   byte half. Result: perspective correct (map low-bytes ~ok) but texture
   garbled (char high-bytes wrong). VRAM `$0000-$3FFF` is 100% populated, so
   *something* is uploaded — the question is whether it's correct.
2. **CGRAM palette wrong.** Dump shows BG palette colors `$00-$05` = black
   (`0000`). Mode-7 BG1 uses CGRAM `$00-$7F` (`pixel & 0x7f`). A bad/partial
   CGRAM upload would mis-color the road. Vivid red/blue argues somewhat against
   pure-palette (structure looks like wrong data, not wrong colors), but verify.
3. **Per-scanline M7 matrix HDMA values wrong** (least likely — perspective
   looks right, but worth ruling out if VRAM+CGRAM match the reference).

## The plan (uses the harness against Mesen2 ground truth)

### Step 1 — Capture both sides on the same track
- **snesrecomp:** dump a Mode-7 frame:
  ```
  SMK_HEADLESS=1 SMK_MAX_FRAMES=1800 SMK_SNAPSHOT_EVERY=1800 \
    SMK_SNAPSHOT_PREFIX=build/recomp_m7 ./build/Debug/smk_launcher.exe "Super Mario Kart (USA).sfc"
  ```
- **Mesen2 (ground truth):** load SMK, reach the attract Mode-7 race, run
  `tools/mesen/dump_snapshot.lua` (tune `DUMP_FRAME` to land on Mode-7). Outputs
  `mesen_m7.bin` in the identical SMKSNAP2 format.
- **Alignment note:** the camera (M7 matrix) differs frame-to-frame and lives in
  registers, not VRAM. The **track char data + tilemap + CGRAM are static per
  track**, so VRAM `$0000-$3FFF` and CGRAM are comparable as long as *both dumps
  are on the same track*. Ensure that (same attract track, or use a known race).

### Step 2 — Diff, focused on Mode-7
```
py tools/diff_snapshots.py mesen_m7.bin build/recomp_m7_f001800.bin --vram-only
```
Interpret:
- **VRAM `$0000-$3FFF` diverges (esp. high bytes):** hypothesis #1 confirmed —
  char-data upload bug. Go to Step 3a.
- **Only CGRAM diverges:** hypothesis #2 — palette upload bug. Go to Step 3b.
- **Neither VRAM nor CGRAM diverges:** hypothesis #3 — the static data is right,
  so the fault is in the per-scanline matrix/HDMA snesrecomp feeds the PPU. Go
  to Step 3c.

### Step 3a — Fix the VRAM upload (custom frame/DMA model)
- Find the DMA that uploads the Mode-7 track gfx (forced-blank, large transfer
  to VRAM `$0000`). Trace with the existing snesrecomp diagnostics
  (`SMK_WATCH_ADDR`, `SMK_TRACE_EXEC`) around race entry.
- Prime suspect: snesrecomp's `begin_frame`/`end_frame` ordering vs. LakeSnes'
  native `snes_runFrame`. **Test:** route one frame through `snes_runFrame` (or
  replicate its DMA timing) and re-diff. If VRAM then matches Mesen, the custom
  model is the culprit — port the missing DMA/VMAIN handling.
- Check `$2115` (VMAIN) increment mode handling for the Mode-7 upload and that
  `$2118`/`$2119` (low/high VRAM data ports) are both serviced.

### Step 3b — Fix the CGRAM upload
- Trace the CGRAM DMA / `$2121`-`$2122` writes at race entry; confirm all 256
  colors land. Compare CGRAM region report against Mesen.

### Step 3c — Fix the per-scanline matrix
- Compare the HDMA-driven `$211B-$2120` (M7 matrix) values snesrecomp applies
  per scanline against Mesen's. Suspect the "untimed HDMA in end_frame" approx.

### Step 4 — Iterate to green
Re-dump, re-diff until VRAM+CGRAM match Mesen, then confirm visually
(`SMK_DUMP_PREFIX` BMP). Done when the road surface renders as the real track.

## Tooling reference
- `snesrecomp_dump_snapshot()` — WRAM+VRAM+CGRAM binary (SMKSNAP2).
- env `SMK_SNAPSHOT_PREFIX`, `SMK_SNAPSHOT_EVERY` — per-frame dumps.
- `tools/diff_snapshots.py [--vram-only] [--stop-on-first] ref test` —
  ref/test may be a `_fNNN` prefix or a single `.bin`.
- `tools/mesen/dump_snapshot.lua` — Mesen2 ground-truth dump, same format.
