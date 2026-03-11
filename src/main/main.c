/*
 * Super Mario Kart — Static Recompilation
 *
 * Now powered by snesrecomp: real SNES hardware (LakeSnes backend)
 * for PPU rendering, SPC700 audio, DMA, and full memory bus.
 */

#include <snesrecomp/snesrecomp.h>
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

    /* Load ROM */
    const char *rom_path = find_rom_path(argc, argv);
    printf("Loading ROM: %s\n", rom_path);
    if (!snesrecomp_load_rom(rom_path)) {
        fprintf(stderr, "Failed to load ROM. Exiting.\n");
        snesrecomp_shutdown();
        return 1;
    }
    printf("\n");

    /* TODO: Register recompiled game functions here */
    /* func_table_register(0x80803A, smk_80803A); */

    printf("Running... (press Escape to quit)\n");

    /* Main loop */
    while (snesrecomp_begin_frame()) {
        /* TODO: Execute recompiled game frame here */
        /* func_table_call(0x80803A); */

        snesrecomp_end_frame();
    }

    printf("Shutting down...\n");
    snesrecomp_shutdown();

    return 0;
}
