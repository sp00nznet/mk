/*
 * lakesnes_ref — native-LakeSnes ground-truth runner for the Mode-7 diagnosis.
 *
 * Runs the original SMK ROM through LakeSnes' OWN frame loop (snes_runFrame),
 * bypassing snesrecomp's custom begin/end_frame + manual HDMA model entirely.
 * Dumps a SMKSNAP2 snapshot (WRAM+VRAM+CGRAM, same format as
 * snesrecomp_dump_snapshot) and a BMP screenshot at the captured frame.
 *
 * Purpose: if native LakeSnes renders the Mode-7 track CLEANLY while snesrecomp
 * renders it garbled, the bug is snesrecomp's frame/DMA model — and diffing the
 * two SMKSNAP2 dumps shows exactly which VRAM/CGRAM region is wrong.
 *
 * Build: CMake target `lakesnes_ref` (links lakesnes_hw).
 * Usage: lakesnes_ref <rom.sfc> <out_prefix> [max_frames] [dump_frame]
 *   dump_frame = 0 (default): auto-capture the first Mode-7 (mode==7) frame
 *                after frame 600; otherwise capture exactly that frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/ppu.h"

static void write_snapshot(Snes *snes, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot open %s\n", path); return; }
    const uint32_t wram_size = 0x20000, vram_size = 0x10000, cgram_size = 0x200;
    fwrite("SMKSNAP2", 1, 8, f);
    fwrite(&wram_size, sizeof(uint32_t), 1, f);
    fwrite(&vram_size, sizeof(uint32_t), 1, f);
    fwrite(&cgram_size, sizeof(uint32_t), 1, f);
    fwrite(snes->ram, 1, wram_size, f);
    fwrite(snes->ppu->vram, 1, vram_size, f);
    fwrite(snes->ppu->cgram, 1, cgram_size, f);
    fclose(f);
}

/* Same BMP layout as snesrecomp_dump_ppu: left 256x224, buffer stride 2048,
 * x step 8, bytes [idx+1]=B [idx+2]=G [idx+3]=R (BGRX default format). */
static void write_bmp(const uint8_t *buf, const char *path) {
    int w = 256, h = 224;
    FILE *bmp = fopen(path, "wb");
    if (!bmp) { printf("cannot open %s\n", path); return; }
    int row_stride = w * 3;
    int pad = (4 - (row_stride % 4)) % 4;
    int data_size = (row_stride + pad) * h;
    int file_size = 54 + data_size;
    uint8_t hdr[54];
    memset(hdr, 0, 54);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size & 0xFF; hdr[3] = (file_size >> 8) & 0xFF;
    hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[26] = 1; hdr[28] = 24;
    hdr[34] = data_size & 0xFF; hdr[35] = (data_size >> 8) & 0xFF;
    hdr[36] = (data_size >> 16) & 0xFF; hdr[37] = (data_size >> 24) & 0xFF;
    fwrite(hdr, 1, 54, bmp);
    uint8_t pad_bytes[3] = {0, 0, 0};
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int idx = y * 2048 + x * 8;
            uint8_t bgr[3] = { buf[idx + 1], buf[idx + 2], buf[idx + 3] };
            fwrite(bgr, 1, 3, bmp);
        }
        if (pad > 0) fwrite(pad_bytes, 1, pad, bmp);
    }
    fclose(bmp);
}

/* Button index map matches snesrecomp main.c (SMK_SCRIPT) and snes_setButtonState:
 * B=0 Y=1 SELECT=2 START=3 UP=4 DOWN=5 LEFT=6 RIGHT=7 A=8 X=9 L=10 R=11 */
static int ref_button_index(const char *n) {
    if (!strcmp(n, "B")) return 0;      if (!strcmp(n, "Y")) return 1;
    if (!strcmp(n, "SELECT")) return 2; if (!strcmp(n, "START")) return 3;
    if (!strcmp(n, "UP")) return 4;     if (!strcmp(n, "DOWN")) return 5;
    if (!strcmp(n, "LEFT")) return 6;   if (!strcmp(n, "RIGHT")) return 7;
    if (!strcmp(n, "A")) return 8;      if (!strcmp(n, "X")) return 9;
    if (!strcmp(n, "L")) return 10;     if (!strcmp(n, "R")) return 11;
    return -1;
}

#define REF_SCRIPT_MAX 64
#define REF_SCRIPT_HOLD 4
static struct { int frame; int btn; } g_script[REF_SCRIPT_MAX];
static int g_script_n = 0;

static void ref_parse_script(void) {
    const char *s = getenv("SMK_REF_SCRIPT");
    if (!s) return;
    char buf[1024];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, ",");
    while (tok && g_script_n < REF_SCRIPT_MAX) {
        char name[32]; int frame;
        if (sscanf(tok, "%d:%31s", &frame, name) == 2) {
            int b = ref_button_index(name);
            if (b >= 0) {
                g_script[g_script_n].frame = frame;
                g_script[g_script_n].btn = b;
                g_script_n++;
                printf("native: scripted input frame %d -> %s\n", frame, name);
            }
        }
        tok = strtok(NULL, ",");
    }
}

/* Apply scripted input for this frame: OR all active windows per button, then
 * push to player 1. Mirrors snesrecomp's apply_script. */
static void ref_apply_script(Snes *snes, int frame) {
    int held[12] = {0};
    for (int i = 0; i < g_script_n; i++)
        if (frame >= g_script[i].frame && frame < g_script[i].frame + REF_SCRIPT_HOLD)
            held[g_script[i].btn] = 1;
    for (int b = 0; b < 12; b++)
        snes_setButtonState(snes, 1, b, held[b] ? true : false);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <rom.sfc> <out_prefix> [max_frames] [dump_frame]\n", argv[0]);
        printf("  env SMK_REF_EVERY=N    periodic snapshot+bmp every N frames\n");
        printf("  env SMK_REF_SCRIPT=\"frame:BTN,...\"  scripted input (START,B,...)\n");
        return 1;
    }
    const char *rom_path = argv[1];
    const char *prefix = argv[2];
    int max_frames = (argc >= 4) ? atoi(argv[3]) : 2200;
    int dump_frame = (argc >= 5) ? atoi(argv[4]) : 0; /* 0 = auto first mode7 */
    int dump_every = 0;
    { const char *e = getenv("SMK_REF_EVERY"); if (e && atoi(e) > 0) dump_every = atoi(e); }

    FILE *f = fopen(rom_path, "rb");
    if (!f) { printf("Can't open %s\n", rom_path); return 1; }
    fseek(f, 0, SEEK_END);
    long rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom_data = malloc(rom_size);
    fread(rom_data, 1, rom_size, f);
    fclose(f);

    Snes *snes = snes_init();
    if (!snes_loadRom(snes, rom_data, (int)rom_size)) {
        printf("snes_loadRom failed\n"); return 1;
    }
    uint8_t *pixels = malloc(512 * 480 * 4);
    printf("native LakeSnes: ROM loaded (%ld bytes). max_frames=%d dump_frame=%s\n",
           rom_size, max_frames, dump_frame ? "fixed" : "auto-mode7");

    ref_parse_script();
    /* SMK_REF_POKE="off:val:fromframe,off:val:fromframe,..." — force WRAM words
     * each frame (debug). e.g. enter mode select: "32:0014:400,160:8000:400". */
    int pk_off[8], pk_val[8], pk_from[8], pk_n = 0;
    { const char *p = getenv("SMK_REF_POKE");
      if (p) { char b[256]; strncpy(b, p, 255); b[255] = 0;
        char *t = strtok(b, ",");
        while (t && pk_n < 8) {
          if (sscanf(t, "%x:%x:%d", &pk_off[pk_n], &pk_val[pk_n], &pk_from[pk_n]) == 3) {
            printf("native: poke $%05X=%04X from f%d\n", pk_off[pk_n], pk_val[pk_n], pk_from[pk_n]);
            pk_n++;
          }
          t = strtok(NULL, ",");
        } } }

    int captured = 0;
    uint16_t last_state = 0xFFFF;
    for (int i = 1; i <= max_frames; i++) {
        if (g_script_n) ref_apply_script(snes, i);
        /* from >= 0: continuous from that frame. from < 0: one-shot at -from. */
        for (int k = 0; k < pk_n; k++)
          if (pk_from[k] >= 0 ? (i >= pk_from[k]) : (i == -pk_from[k])) {
            snes->ram[pk_off[k]] = pk_val[k] & 0xFF;
            snes->ram[pk_off[k] + 1] = (pk_val[k] >> 8) & 0xFF;
        }
        snes_runFrame(snes);
        int mode = snes->ppu->mode;
        uint16_t state = snes->ram[0x36] | (snes->ram[0x37] << 8);
        uint16_t sub   = snes->ram[0x32] | (snes->ram[0x33] << 8);
        if (state != last_state) {
            printf("  [f%04d] STATE $36=%04X $32=%04X mode=%d bright=%d\n",
                   i, state, sub, mode, snes->ppu->brightness);
            last_state = state;
        }
        if (i % 120 == 0)
            printf("  frame %4d: mode=%d brightness=%d $36=%04X\n",
                   i, mode, snes->ppu->brightness, state);

        int periodic = dump_every && (i % dump_every == 0);
        int do_dump = dump_frame ? (i == dump_frame)
                                 : (!captured && !dump_every && mode == 7 &&
                                    snes->ppu->brightness >= 8 && i > 600);
        if (do_dump || periodic) {
            captured = 1;
            char path[600];
            snes_setPixels(snes, pixels);
            snprintf(path, sizeof(path), "%s_f%06d.bin", prefix, i);
            write_snapshot(snes, path);
            snprintf(path, sizeof(path), "%s_f%06d.bmp", prefix, i);
            write_bmp(pixels, path);
            if (!periodic)
                printf("native LakeSnes: captured frame %d (mode=%d) -> %s_f%06d.{bin,bmp}\n",
                       i, mode, prefix, i);
            if (do_dump && dump_frame) break;
        }
    }
    if (!captured) printf("native LakeSnes: never reached a Mode-7 frame.\n");

    snes_free(snes);
    free(rom_data);
    free(pixels);
    return 0;
}
