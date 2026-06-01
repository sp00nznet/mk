/*
 * Super Mario Kart — Static Recompilation
 *
 * Powered by snesrecomp (LakeSnes backend) for real SNES hardware.
 * Recompiled game code drives the hardware via bus_read8/bus_write8.
 */

#include <snesrecomp/snesrecomp.h>
#include "smk/functions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* LakeSnes button injection (for scripted/automated input) */
extern struct Snes *snesrecomp_get_snes(void);
void snes_setButtonState(struct Snes *snes, int player, int button, bool pressed);
void snes_doAutoJoypad(struct Snes *snes);

static const char *find_rom_path(int argc, char *argv[]) {
    if (argc >= 2) return argv[1];
    return "Super Mario Kart (USA).sfc";
}

/*
 * Scripted input for automated testing.
 *
 * Set SMK_SCRIPT to a comma-separated list of "frame:button" pairs, e.g.
 *   SMK_SCRIPT="300:START,360:B,420:B"
 * The named button is held for ~4 frames starting at the given frame.
 * Buttons: B Y SELECT START UP DOWN LEFT RIGHT A X L R
 * Set SMK_MAX_FRAMES to auto-exit after N frames (dumps PPU on exit).
 * Set SMK_DUMP_PREFIX to control where periodic PPU dumps are written.
 */
static int script_button_index(const char *name) {
    if (!strcmp(name, "B")) return 0;
    if (!strcmp(name, "Y")) return 1;
    if (!strcmp(name, "SELECT")) return 2;
    if (!strcmp(name, "START")) return 3;
    if (!strcmp(name, "UP")) return 4;
    if (!strcmp(name, "DOWN")) return 5;
    if (!strcmp(name, "LEFT")) return 6;
    if (!strcmp(name, "RIGHT")) return 7;
    if (!strcmp(name, "A")) return 8;
    if (!strcmp(name, "X")) return 9;
    if (!strcmp(name, "L")) return 10;
    if (!strcmp(name, "R")) return 11;
    return -1;
}

#define SCRIPT_MAX 32
static struct { int frame; int btn; } s_script[SCRIPT_MAX];
static int s_script_n = 0;
#define SCRIPT_HOLD 4

static void parse_script(void) {
    const char *s = getenv("SMK_SCRIPT");
    if (!s) return;
    char buf[512];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, ",");
    while (tok && s_script_n < SCRIPT_MAX) {
        char name[32];
        int frame;
        if (sscanf(tok, "%d:%31s", &frame, name) == 2) {
            int b = script_button_index(name);
            if (b >= 0) {
                s_script[s_script_n].frame = frame;
                s_script[s_script_n].btn = b;
                s_script_n++;
                printf("smk: scripted input frame %d -> %s\n", frame, name);
            }
        }
        tok = strtok(NULL, ",");
    }
}

/* When locking input (scripted/headless runs), the script is the ONLY source
 * of input — the SDL keyboard read in begin_frame is fully overridden so stray
 * window focus / keypresses can't perturb a deterministic run. */
static int s_lock_input = 0;

static void apply_script(int frame) {
    struct Snes *snes = snesrecomp_get_snes();
    if (!snes || (!s_lock_input && s_script_n == 0)) return;
    /* OR the active windows per button — multiple entries may target the same
     * button at different frames, so a later inactive entry must not clear an
     * earlier active one. */
    int held[12] = {0};
    for (int i = 0; i < s_script_n; i++) {
        if (frame >= s_script[i].frame &&
            frame < s_script[i].frame + SCRIPT_HOLD) {
            held[s_script[i].btn] = 1;
        }
    }
    for (int b = 0; b < 12; b++) {
        snes_setButtonState(snes, 1, b, held[b] ? true : false);
    }
    /* begin_frame already ran the auto-joypad read against the keyboard state
     * (all-clear in headless/scripted runs); re-run it so the injected buttons
     * land in the auto-read registers ($4218-) for this frame. */
    snes_doAutoJoypad(snes);
}

int main(int argc, char *argv[]) {
    printf("Super Mario Kart - Static Recompilation\n");
    printf("========================================\n");
    printf("Powered by snesrecomp (LakeSnes backend)\n\n");

    /* Initialize snesrecomp (LakeSnes hardware + SDL2 platform) */
    if (!snesrecomp_init("Super Mario Kart", 3)) {
        fprintf(stderr, "Failed to initialize snesrecomp. Exiting.\n");
        return 1;
    }

    /* Load ROM (LakeSnes auto-detects HiROM, sets up cart mapping) */
    const char *rom_path = find_rom_path(argc, argv);
    printf("Loading ROM: %s\n", rom_path);
    if (!snesrecomp_load_rom(rom_path)) {
        fprintf(stderr, "Failed to load ROM. Exiting.\n");
        snesrecomp_shutdown();
        return 1;
    }
    printf("\n");

    /* Recompiled functions are auto-registered via RECOMP_PATCH static
     * constructors — see ext/snesrecomp/include/snesrecomp/recomp_patch.h. */

    /* Force-interpret mode: run the genuine ROM for init/transitions/state
     * handlers via the LakeSnes CPU, using the recompiled frame shells only
     * for orchestration. Lets the full game loop run while recompiled
     * functions are still partial. Default ON; set SMK_INTERP=0 to prefer
     * recompiled functions where available. */
    {
        const char *interp = getenv("SMK_INTERP");
        bool force = (interp == NULL) || (atoi(interp) != 0);
        recomp_interp_set_force(force);
        printf("smk: interpreter force mode = %s\n", force ? "ON" : "OFF");
    }

    /* === Run the boot chain === */
    printf("--- Running boot chain ---\n");
    smk_80FF70();
    printf("--- Boot chain done ---\n\n");

    printf("Running... (press Escape to quit)\n\n");

    parse_script();
    s_lock_input = (getenv("SMK_SCRIPT") != NULL) || (getenv("SMK_HEADLESS") != NULL);
    int max_frames = 0;
    {
        const char *mf = getenv("SMK_MAX_FRAMES");
        if (mf) max_frames = atoi(mf);
    }
    const char *dump_prefix = getenv("SMK_DUMP_PREFIX");
    /* Lockstep snapshot dump (WRAM+VRAM) for divergence analysis. */
    const char *snapshot_prefix = getenv("SMK_SNAPSHOT_PREFIX");
    int snapshot_every = 1;
    {
        const char *se = getenv("SMK_SNAPSHOT_EVERY");
        if (se && atoi(se) > 0) snapshot_every = atoi(se);
    }
    int frame_no = 0;

    /* === Main frame loop === */
    while (snesrecomp_begin_frame()) {
        /* Apply scripted input (after begin_frame's keyboard read) */
        apply_script(frame_no);

        /* Hybrid: navigate menus with recompiled handlers, then latch into
         * force-interpret once a target state is reached so the genuine ROM
         * drives gameplay. SMK_FORCE_FROM_STATE=<hex $36 value>. */
        {
            const char *ffs = getenv("SMK_FORCE_FROM_STATE");
            if (ffs) {
                static int latched = 0;
                if (!latched && bus_wram_read16(0x36) == (uint16_t)strtol(ffs, NULL, 16)) {
                    latched = 1;
                    recomp_interp_set_force(true);
                    printf("smk: force-interpret latched at state $%s (frame %d)\n",
                           ffs, frame_no);
                }
            }
        }

        /* Clear NMI flag before NMI handler — the handler itself sets $44
         * (smk_808237 sets $44=$FFFF, smk_8081DD sets $44=1).
         * If we pre-set $44=1, state $04's BNE check exits early. */
        bus_wram_write16(g_cpu.DP + 0x44, 0);

        /* Phase markers for DMA-setup tracing (correlate DMAs to NMI vs main
         * and to frame boundaries). */
        if (getenv("SMK_DMASETUP_DEBUG"))
            fprintf(stderr, "==== frame %d : NMI (smk_808000) ====\n", frame_no);

        /* Run NMI handler (DMA, brightness, NMI state dispatch) */
        smk_808000();

        if (getenv("SMK_DMASETUP_DEBUG"))
            fprintf(stderr, "==== frame %d : MAIN (smk_808056) ====\n", frame_no);

        /* Run one main loop iteration (frame setup + state handler) */
        smk_808056();

        /* Render PPU and present */
        snesrecomp_end_frame();

        frame_no++;

        /* Lockstep snapshot dump (WRAM+VRAM). SMK_SNAPSHOT_PREFIX=<dir/prefix>
         * writes <prefix>_fNNNNNN.bin every SMK_SNAPSHOT_EVERY frames. Run once
         * as a pure-interpreter reference and once with SMK_INTERP=0, then
         * tools/diff_snapshots.py reports the first diverging address. */
        if (snapshot_prefix && (frame_no % snapshot_every == 0)) {
            char path[512];
            snprintf(path, sizeof(path), "%s_f%06d.bin", snapshot_prefix, frame_no);
            snesrecomp_dump_snapshot(path);
        }

        /* Periodic PPU dump for automated inspection */
        if (dump_prefix) {
            char path[512];
            if (frame_no % 120 == 0) {
                snprintf(path, sizeof(path), "%s_f%04d.log", dump_prefix, frame_no);
                snesrecomp_dump_ppu(path);
            }
        }

        if (getenv("SMK_TRACE_STATE")) {
            static uint16_t last36 = 0xABCD, last32 = 0xABCD;
            uint16_t s36 = bus_wram_read16(0x36), s32 = bus_wram_read16(0x32);
            if (s36 != last36 || s32 != last32) {
                printf("smk[f%d]: STATE $36=%04X  $32=%04X\n", frame_no, s36, s32);
                last36 = s36; last32 = s32;
            }
        }

        if (getenv("SMK_TRACE_STATE") && frame_no % 120 == 0) {
            printf("smk[f%d]: $36=%04X $32=%04X $0158=%04X $0172=%04X $80=%04X\n",
                   frame_no, bus_wram_read16(0x36), bus_wram_read16(0x32),
                   bus_wram_read16(0x0158), bus_wram_read16(0x0172),
                   bus_wram_read16(0x80));
        }

        if (max_frames > 0 && frame_no >= max_frames) {
            if (dump_prefix) {
                char path[512];
                snprintf(path, sizeof(path), "%s_final.log", dump_prefix);
                snesrecomp_dump_ppu(path);
            }
            printf("smk: reached SMK_MAX_FRAMES=%d, exiting\n", max_frames);
            break;
        }
    }

    printf("Shutting down...\n");
    snesrecomp_shutdown();

    return 0;
}
