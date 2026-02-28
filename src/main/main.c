#include "recomp/cpu.h"
#include "recomp/memory.h"
#include "recomp/func_table.h"
#include "hal/ppu.h"
#include "hal/apu.h"
#include "hal/dma.h"
#include "hal/dsp1.h"
#include "hal/io.h"
#include "platform/sdl_backend.h"
#include "platform/input.h"
#include <stdio.h>
#include <stdlib.h>

static const char *find_rom_path(int argc, char *argv[]) {
    if (argc >= 2) return argv[1];
    /* Try default name in current directory */
    return "Super Mario Kart (USA).sfc";
}

int main(int argc, char *argv[]) {
    printf("Super Mario Kart - Static Recompilation\n");
    printf("========================================\n\n");

    /* --- Initialize subsystems --- */
    cpu_reset();
    mem_init();
    func_table_init();
    ppu_init();
    apu_init();
    dma_init();
    dsp1_init();
    io_init();

    /* --- Load ROM --- */
    const char *rom_path = find_rom_path(argc, argv);
    printf("Loading ROM: %s\n", rom_path);
    if (!mem_load_rom(rom_path)) {
        fprintf(stderr, "Failed to load ROM. Exiting.\n");
        return 1;
    }
    printf("ROM loaded successfully.\n\n");

    /* --- Initialize SDL2 --- */
    if (!sdl_init()) {
        fprintf(stderr, "Failed to initialize SDL2. Exiting.\n");
        return 1;
    }

    printf("Running... (press Escape to quit)\n");

    /* --- Main loop --- */
    int running = 1;
    while (running) {
        /* Poll events and check for quit */
        if (!sdl_poll_events()) {
            running = 0;
            break;
        }

        /* Update input state */
        input_update();

        /* Update joypad auto-read registers */
        if (g_io.auto_joypad_read) {
            g_io.joy[0] = input_read_joypad(0);
            g_io.joy[1] = input_read_joypad(1);
        }

        /* TODO: Execute recompiled game frame here */

        /* Render frame (stub: backdrop color only) */
        ppu_end_frame();

        /* Present framebuffer */
        sdl_present_frame(g_ppu.framebuffer);

        /* Frame timing */
        sdl_frame_sync();
    }

    /* --- Shutdown --- */
    printf("Shutting down...\n");
    sdl_shutdown();

    return 0;
}
