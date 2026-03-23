/*
 * OAM Dumper — Runs original SMK ROM in LakeSnes for N frames,
 * then dumps the OAM sprite table and PPU state.
 *
 * Build: cl /I ext/snesrecomp/ext/LakeSnes oam_dumper.c ext/snesrecomp/ext/LakeSnes/snes/*.c
 * Usage: oam_dumper.exe "Super Mario Kart (USA).sfc" [frames]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/ppu.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <rom.sfc> [frames]\n", argv[0]);
        return 1;
    }

    int target_frames = (argc >= 3) ? atoi(argv[2]) : 300;

    /* Load ROM */
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("Can't open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom_data = malloc(rom_size);
    fread(rom_data, 1, rom_size, f);
    fclose(f);

    /* Init SNES */
    Snes *snes = snes_init();
    snes_loadRom(snes, rom_data, (int)rom_size);
    printf("ROM loaded (%ld bytes). Running %d frames...\n", rom_size, target_frames);

    /* Run frames */
    for (int i = 0; i < target_frames; i++) {
        snes_runFrame(snes);
    }
    printf("Done. Dumping OAM...\n\n");

    /* Read OAM from PPU */
    Ppu *ppu = snes->ppu;

    printf("=== PPU State ===\n");
    printf("forcedBlank=%d brightness=%d mode=%d\n",
           ppu->forcedBlank, ppu->brightness, ppu->mode);
    printf("OBSEL: objSize=%d objTileAdr1=$%04X objTileAdr2=$%04X\n",
           ppu->objSize, ppu->objTileAdr1, ppu->objTileAdr2);
    printf("TM mainScreenEnabled: %d %d %d %d %d\n",
           ppu->layer[0].mainScreenEnabled,
           ppu->layer[1].mainScreenEnabled,
           ppu->layer[2].mainScreenEnabled,
           ppu->layer[3].mainScreenEnabled,
           ppu->layer[4].mainScreenEnabled);
    printf("BG tilemapAdr: %04X %04X %04X %04X\n",
           ppu->bgLayer[0].tilemapAdr, ppu->bgLayer[1].tilemapAdr,
           ppu->bgLayer[2].tilemapAdr, ppu->bgLayer[3].tilemapAdr);
    printf("BG tileAdr: %04X %04X %04X %04X\n",
           ppu->bgLayer[0].tileAdr, ppu->bgLayer[1].tileAdr,
           ppu->bgLayer[2].tileAdr, ppu->bgLayer[3].tileAdr);

    printf("\n=== Active OAM Sprites ===\n");
    int active = 0;
    for (int i = 0; i < 128; i++) {
        uint8_t x_lo = ppu->oam[i * 4 + 0];
        uint8_t y = ppu->oam[i * 4 + 1];
        uint8_t tile = ppu->oam[i * 4 + 2];
        uint8_t attr = ppu->oam[i * 4 + 3];

        int hi_idx = 0x200 + (i / 4);
        int hi_shift = (i % 4) * 2;
        uint8_t hi_bits = (ppu->oam[hi_idx] >> hi_shift) & 0x03;
        int x9 = hi_bits & 1;
        int size = (hi_bits >> 1) & 1;

        int full_x = x_lo + (x9 ? 256 : 0);

        /* Skip offscreen sprites */
        if (y == 0xE0 && x_lo == 0xF8) continue;
        if (y >= 0xE0 && full_x >= 0xF0) continue;

        int pal = (attr >> 1) & 7;
        int pri = (attr >> 4) & 3;
        int nt = attr & 1;
        int hf = (attr >> 6) & 1;
        int vf = (attr >> 7) & 1;

        printf("  [%3d] x=%3d y=%3d tile=$%02X nt=%d pal=%d pri=%d hf=%d vf=%d %s\n",
               i, full_x, y, tile, nt, pal, pri, hf, vf,
               size ? "16x16" : "8x8");
        active++;
    }
    printf("Total active: %d\n", active);

    /* Dump VRAM at kart sprite tile areas */
    printf("\n=== VRAM Sprite Tile Area ($5800-$5CC0) ===\n");
    for (int base = 0x5800; base <= 0x5CC0; base += 0x40) {
        int nz = 0;
        for (int j = 0; j < 0x40; j++) {
            if (ppu->vram[base + j] != 0) nz++;
        }
        if (nz > 0) {
            printf("  $%04X: %d/64 words nonzero  [%04X %04X %04X %04X ...]\n",
                   base, nz, ppu->vram[base], ppu->vram[base+1],
                   ppu->vram[base+2], ppu->vram[base+3]);
        }
    }

    /* Cleanup */
    snes_free(snes);
    free(rom_data);
    printf("\nDone.\n");
    return 0;
}
