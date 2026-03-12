/*
 * Super Mario Kart — Static Recompilation
 *
 * Powered by snesrecomp (LakeSnes backend) for real SNES hardware.
 * Recompiled game code drives the hardware via bus_read8/bus_write8.
 */

#include <snesrecomp/snesrecomp.h>
#include "smk/functions.h"
#include "smk/cpu_ops.h"
#include <stdio.h>
#include <stdlib.h>

static const char *find_rom_path(int argc, char *argv[]) {
    if (argc >= 2) return argv[1];
    return "Super Mario Kart (USA).sfc";
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

    /* Register all recompiled functions */
    smk_register_all();

    /* === Run the boot chain === */
    printf("--- Running boot chain ---\n");
    smk_80FF70();
    printf("--- Boot chain done ---\n\n");

    printf("Running... (press Escape to quit)\n\n");

    /* === Main frame loop === */
    while (snesrecomp_begin_frame()) {
        /* Simulate NMI: set DP $44 so main loop doesn't spin */
        bus_wram_write16(g_cpu.DP + 0x44, 1);

        /* Run NMI handler (DMA, brightness, NMI state dispatch) */
        smk_808000();

        /* Run one main loop iteration (frame setup + state handler) */
        smk_808056();

        /* Render PPU and present */
        snesrecomp_end_frame();

        {
            static int fc = 0;
            if (fc == 30) {
                snesrecomp_dump_ppu("D:/recomp/snes/mk/ppu_state.log");
            }
            fc++;
        }
    }

    printf("Shutting down...\n");
    snesrecomp_shutdown();

    return 0;
}
