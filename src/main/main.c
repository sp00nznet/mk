/*
 * Super Mario Kart — Static Recompilation
 *
 * Powered by snesrecomp (LakeSnes backend) for real SNES hardware.
 * Recompiled game code drives the hardware via bus_read8/bus_write8.
 */

#include <snesrecomp/snesrecomp.h>
#include <snesrecomp/menu_overlay.h>
#include <snesrecomp/mp_session.h>
#include "smk/functions.h"

#define SMK_STATE_PATH "smk_state.sav"

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
     * constructors — see ext/snesrecomp/include/snesrecomp/recomp_patch.h.
     * smk_autogen.c has no externally-referenced symbol, so force its TU to link
     * (else the linker drops it and its registrations never run). */
    smk_autogen_link_anchor();

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

    /* Real-frame mode is the DEFAULT: bypass the recompiled boot chain +
     * per-frame shells and run the genuine ROM via LakeSnes's full cycle-accurate
     * frame (like tools/lakesnes_ref). This renders and plays the whole game,
     * including the Mode-7 race the shells can't yet drive (their dropped vblank
     * waits leave the race's multi-frame init/fade-in incomplete). The CPU boots
     * from the reset vector on the first frame.
     *
     * Set SMK_SHELLS=1 to use the recompiled per-frame shells instead — the path
     * for incremental recompilation development. (SMK_REALFRAME=1 also forces
     * real-frame, for explicitness.) */
    bool realframe = (getenv("SMK_SHELLS") == NULL) || (getenv("SMK_REALFRAME") != NULL);

    /* Timed-recomp (Phase-1 of replacing real-frame mode with static recomp):
     * run the real-frame timed loop (real PPU/APU/NMI timing) BUT intercept
     * registered recompiled functions, executing native C in place of the ROM
     * subroutine. Proves the static-recomp execution model end-to-end one
     * function at a time, validated bit-identical to real-frame via the
     * snapshot-diff harness. Implies real-frame timing. */
    bool recomp  = (getenv("SMK_RECOMP") != NULL);
    bool profile = (getenv("SMK_RECOMP_PROFILE") != NULL);
    if (recomp || profile) realframe = true;

    /* === Run the boot chain (shell mode only) === */
    if (!realframe) {
        printf("smk: SHELL mode (recompiled per-frame shells)\n");
        printf("--- Running boot chain ---\n");
        smk_80FF70();
        printf("--- Boot chain done ---\n\n");
    } else if (recomp) {
        printf("smk: TIMED-RECOMP mode (real-frame timing + recompiled-function interception)\n");
    } else {
        printf("smk: REAL-FRAME mode (full LakeSnes execution) — set SMK_SHELLS=1 for the recomp shell path\n");
    }

    /* Profiling-only mode: install the hook to rank the hottest JSR/JSL targets
     * (Phase-3 recompilation candidates), without intercepting anything. Dumps
     * the top-N at exit. */
    if (profile) {
        recomp_timed_recomp_enable();
        recomp_timed_profile_enable();
        printf("smk: TIMED-RECOMP PROFILE mode (ranking JSR/JSL call targets)\n");
    }

    /* Install the interception hook + register the intercept set. Default is the
     * single OAM-DMA leaf $80:946E (RTS/near). Override with
     * SMK_RECOMP_INTERCEPTS="80946E,81xxxx:L,..." (comma-separated 24-bit hex;
     * suffix ':L' marks a JSL/RTL-return routine, default JSR/RTS). */
    if (recomp) {
        recomp_timed_recomp_enable();
        const char *iv = getenv("SMK_RECOMP_INTERCEPTS");
        if (iv && *iv) {
            char buf[256];
            strncpy(buf, iv, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
            for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
                bool is_long = (strchr(t, 'L') || strchr(t, 'l')) != NULL;
                uint32_t a = (uint32_t)strtoul(t, NULL, 16);
                recomp_timed_add_intercept(a, is_long);
                printf("smk: intercept $%06X (%s)\n", a, is_long ? "RTL" : "RTS");
            }
        } else {
            /* Default set: steady-state leaves each validated functionally exact
             * vs the emulation oracle (live WRAM + VRAM + CGRAM identical; only
             * dead stack scratch differs — gate with --ignore-wram 1F00-1FFF).
             * The boot OAM-DMA $80946E (Phase-1 demo) is rendered-faithful but
             * perturbs audio WRAM phase; reach it via SMK_RECOMP_INTERCEPTS. */
            /* autogen (autogen.py + batch.py), all PURE-LOGIC, gated byte-identical
             * THROUGH A RACE (title + Mode-7 gameplay) as a combined set. All 16
             * race-validated funcs live in smk_autogen.c and pass INDIVIDUALLY; this
             * is the largest combined-clean default. Hardware/timing-sensitive readers
             * (e.g. the $4218 input handler $808445) are excluded from the combined
             * default — intercepting them compounds the instant-execution timing skew
             * past live state. Add more via SMK_RECOMP_INTERCEPTS. */
            recomp_timed_add_intercept(0x80F90A, false);   /* race object loop (hot) */
            recomp_timed_add_intercept(0x80A01F, false);
            recomp_timed_add_intercept(0x80A027, false);
            recomp_timed_add_intercept(0x818902, false);   /* Mode-7 engine */
            recomp_timed_add_intercept(0x81B9A8, false);   /* Mode-7 engine */
            recomp_timed_add_intercept(0x808BBF, false);
            recomp_timed_add_intercept(0x8086A0, false);
            recomp_timed_add_intercept(0x80BBCC, false);
            printf("smk: intercept 8 autogen funcs (incl. Mode-7), race-validated combined\n");
        }
    }

    printf("Running... (press Escape to quit)\n\n");

    /* Netplay auto-connect for testing/automation (the menu does this
     * interactively): SMK_MP_HOST=<port> or SMK_MP_JOIN=<ip>:<port>. Netplay
     * runs in real-frame mode (lockstep in snesrecomp_realframe_begin). */
    {
        const char *mph = getenv("SMK_MP_HOST");
        const char *mpj = getenv("SMK_MP_JOIN");
        const char *mpd = getenv("SMK_MP_DELAY");
        if (mpd) mp_set_delay(atoi(mpd));   /* host: input-delay frames (sent to client) */
        if (mph) { mp_host(atoi(mph)); }
        else if (mpj) {
            char ip[64]; int port = 0;
            if (sscanf(mpj, "%63[^:]:%d", ip, &port) == 2) mp_join(ip, port);
        }
    }

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
    while (realframe ? snesrecomp_realframe_begin() : snesrecomp_begin_frame()) {
        /* Apply scripted input (after the input read; overrides keyboard). */
        apply_script(frame_no);

        /* File-menu emulator controls (New=reset, Save/Load = save-state). */
        if (menu_overlay_take_reset()) {
            snesrecomp_reset();
            if (!realframe) smk_80FF70();   /* shell mode: re-run the recompiled boot chain */
            frame_no = 0;
        }
        if (menu_overlay_take_save_state()) snesrecomp_save_state(SMK_STATE_PATH);
        if (menu_overlay_take_load_state()) snesrecomp_load_state(SMK_STATE_PATH);

        /* Scripted save/load at a frame (testing/automation):
         * SMK_SAVESTATE_AT=<frame> / SMK_LOADSTATE_AT=<frame>. */
        {
            const char *sa = getenv("SMK_SAVESTATE_AT");
            const char *la = getenv("SMK_LOADSTATE_AT");
            if (sa && frame_no == atoi(sa)) snesrecomp_save_state(SMK_STATE_PATH);
            if (la && frame_no == atoi(la)) snesrecomp_load_state(SMK_STATE_PATH);
        }

        if (realframe) {
            /* Full cycle-accurate frame on the LakeSnes CPU (CPU + NMI + HDMA +
             * PPU). No recompiled shells. */
            snesrecomp_realframe_end();
        } else {
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
        }

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

        /* Timed-recomp diagnostics: confirm the interception fired and let the
         * WRAM checksum be diffed against a plain real-frame run at the same
         * frame (bit-identical => the recompiled function is faithful). */
        if (recomp && frame_no % 60 == 0) {
            printf("smk[f%d]: intercept_hits=%lu wram_csum=%08X\n",
                   frame_no, recomp_timed_intercept_hits(), snesrecomp_wram_checksum());
        }

        /* Report netplay state transitions. */
        {
            static int last_mp = -1;
            int s = (int)mp_get_state();
            if (s != last_mp) { printf("MP state -> %d (%s)\n", s, mp_status_text()); last_mp = s; }
        }

        /* Netplay lockstep verification: matching checksums on both peers (at the
         * same lockstep frame) confirm deterministic sync. */
        if (mp_get_state() == MP_CONNECTED) {
            static int mp_ls = -1;
            mp_ls++;
            if (mp_ls % 120 == 0)
                printf("MP lockstep f%d: wram_csum=%08X\n", mp_ls, snesrecomp_wram_checksum());
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

    if (profile) recomp_timed_profile_dump(40);

    printf("Shutting down...\n");
    snesrecomp_shutdown();

    return 0;
}
