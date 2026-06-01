# Mode-7 Garbled Track — Diagnosis & Fix Plan

_Last updated: 2026-06-01_

## ⭐ EXECUTED DIAGNOSIS — what we now KNOW (2026-06-01)

The plan below was executed. Findings, in order of certainty:

1. **The bug is 100% in snesrecomp's custom frame model.** `tools/lakesnes_ref`
   runs the identical ROM through LakeSnes' native `snes_runFrame` and renders
   the Mode-7 track **perfectly** (clean road, starting grid, karts, HUD) — see
   `build/ref/native_*.bmp`. snesrecomp garbles it. Same ROM, same PPU code,
   same DSP-1. ⇒ renderer, ROM, DSP-1, Mode-7 matrix, and char-data are all
   exonerated. The fault is snesrecomp's manual begin/end_frame + HDMA driver.

2. **It is a CGRAM (palette) corruption, not VRAM.** Diffing native vs
   snesrecomp SMKSNAP2 dumps (`--vram-only`): VRAM `$0000-$5803` is byte-
   identical (the whole Mode-7 tilemap+char region `$0000-$3FFF` is correct).
   Only CGRAM diverges. Mode-7 BG1 uses CGRAM `$00-$7F`; the road indexes into a
   wrong palette ⇒ the red/blue garble.

3. **The palette SOURCE is correct.** The race palette is decompressed to WRAM
   `$7E:3A80` (512 B) then uploaded to CGRAM. `$7E:3A80` is byte-identical
   (512/512) to native and holds the right colors. So decompression is fine.

4. **The corruption is a transient at race-start (~frame 1797).** Per-frame
   CGRAM snapshots: correct track palette is present & stable f1728–f1794, then
   in one ~6-frame window CGRAM is clobbered to garbage (`0000×6 0518 40BF`,
   123 entries change) and **stays stuck forever** (native completes the intro
   cleanly). This matches the broken intro never finishing.

5. **HDMA does NOT clobber CGRAM.** `SMK_HDMA_DEBUG`: the active Mode-7 HDMA
   channels target `$211B-$211E` (matrix), `$2105` (BGMODE), `$212C` (TM),
   `$2126` (window) — none touch `$2122`/`$2121`; CGRAM is identical before/after
   the scanline loop.

6. **CONCLUSIVE ROOT CAUSE — stale DMA channel registers at race start.**
   `SMK_DMA_LOG` (in shared `dma_doDma`) traces every CGRAM GPDMA with its CGADD
   in BOTH builds. Native vs recomp at race start:

   ```
   NATIVE (clean, repeats every frame):
     src=7E:3BC0 size=2C CGADD=A0   OBJ pal $A0-B5 (WRAM)
     src=7E:3B80 size=2C CGADD=80   OBJ pal $80-95 (WRAM)
   RECOMP (broken):
     src=7E:3BC0 size=2C CGADD=A0   ✓
     src=C4:5D00 size=80 CGADD=B6   ✗ STALE src (ROM, from a prior VRAM DMA), CGADD not reset
     src=C4:5B00 size=80 CGADD=F6   ✗ STALE src, wraps CGADD $F6→$FF→$00-$35
   ```

   Recomp **fires `$420B` CGRAM-DMA triggers without the per-DMA channel setup**
   (`$4302/$4304/$4305` src+size, `$2121` CGADD) that native performs. It reuses
   stale `$C4`-ROM source + `$0080` size left from earlier VRAM *tile* DMAs, and
   CGADD auto-increments instead of resetting — so non-palette ROM data wraps
   into CGRAM `$00-$35` and destroys the Mode-7 BG palette. (Verified: a
   simulation that applies the recomp DMA list reproduces the exact garbage
   bit-for-bit; the `$C4:5B00/5D00` bytes are `0000 0001 0002 0003…`, clearly
   not palette colors.) Native instead runs a tidy 2-entry WRAM OBJ-palette
   refresh loop and never wraps.

   ⇒ The race-intro **control flow diverges** under snesrecomp's frame model:
   the game skips the OBJ-palette DMA-setup instructions but still hits the
   `$420B` trigger. This is the frame-model bug surfacing as game-logic
   divergence.

   **PINNED MECHANISM (SMK_DMASETUP_DEBUG, traces $4300-06/$2121/$420B + PC):**
   ```
   80:83ED  $4300=02 $4301=22   B-addr=$2122 (CGRAM), mode 2
   80:8411  $2121=A0            CGADD=$A0
   80:8439  $420B               DMA1: 7E:3BC0 -> CGRAM  OK
   80:8E2B  src=C4:5D00 sz=80   (no $4300/$4301, no $2121!)
   80:8E43  $420B               DMA2: C4:5D00 -> still $2122  BAD
   80:8E2B  src=C4:5B00 sz=80
   80:8E43  $420B               DMA3: C4:5B00 -> still $2122  BAD
   80:8E09  $4300=01 $4301=18   B-addr=$2118 (VRAM)  <-- only AFTER DMA2/DMA3
   ```
   The VRAM-tile DMA loop body at `80:8E2B` ran twice **without its `80:8E09`
   B-address setup** (`$4301=$18`/VRAM). Those transfers inherited the stale
   `$2122` B-address from the OBJ-palette upload (`80:83ED`) and DMA'd VRAM tile
   data into the Mode-7 palette. ⇒ recomp **enters the VRAM-DMA loop mid-body**,
   skipping `80:8E09`. A control-flow divergence under the frame model.

   **FIX DIRECTION (next):** find why PC reaches `80:8E2B` without `80:8E09` for
   the first 1-2 iterations. Disassemble `$80:8E00-8E50` to learn the loop's
   structure and entry points, then determine what makes recomp enter mid-loop:
     - An NMI/frame boundary firing mid-routine where snesrecomp's manual NMI
       (`smk_808000`) resumes at the wrong PC. Suspect the NMI/main/end_frame
       split and the `$44` NMI-flag handling.
     - A wrong branch from a status read (`$4212`/APU/NMI-count) snesrecomp fakes.
   Validate with `lakesnes_ref` (stays clean) + `SMK_DMA_LOG` parity (recomp must
   show native's `7E:3BC0@A0 / 7E:3B80@80` loop, no `$C4` sources), then a race
   screenshot. Diagnostic: `SMK_DMASETUP_DEBUG` (in `bus.c`).

Tooling added: `tools/lakesnes_ref.c` (native ground truth), `SMK_DMA_DEBUG` /
`SMK_CG_DEBUG` / `SMK_HDMA_DEBUG` in snesrecomp, CGRAM in SMKSNAP2,
`diff_snapshots.py --vram-only`.

---
_Original plan (Mesen2 path — superseded by the self-contained lakesnes_ref
ground truth above, but kept for reference):_

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
