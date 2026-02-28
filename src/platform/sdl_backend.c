#include "platform/sdl_backend.h"
#include <SDL.h>
#include <stdio.h>

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;
static uint64_t      s_frame_start = 0;

/* NTSC frame time: ~16.6393 ms (60.098 Hz) */
static const double FRAME_TIME_MS = 1000.0 / 60.098;

bool sdl_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    s_window = SDL_CreateWindow(
        "Super Mario Kart",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SMK_WINDOW_WIDTH, SMK_WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return false;
    }

    s_texture = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_RGBX8888,
        SDL_TEXTUREACCESS_STREAMING,
        SMK_RENDER_WIDTH, SMK_RENDER_HEIGHT);
    if (!s_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return false;
    }

    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    SDL_RenderPresent(s_renderer);

    s_frame_start = SDL_GetPerformanceCounter();
    return true;
}

void sdl_present_frame(const uint32_t *framebuffer) {
    SDL_UpdateTexture(s_texture, NULL, framebuffer,
                      SMK_RENDER_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

bool sdl_poll_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            return false;
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE)
                return false;
            break;
        default:
            break;
        }
    }
    return true;
}

void sdl_frame_sync(void) {
    uint64_t now = SDL_GetPerformanceCounter();
    double elapsed_ms = (double)(now - s_frame_start) * 1000.0
                        / (double)SDL_GetPerformanceFrequency();
    double remaining = FRAME_TIME_MS - elapsed_ms;
    if (remaining > 1.0) {
        SDL_Delay((uint32_t)(remaining - 0.5));
    }
    s_frame_start = SDL_GetPerformanceCounter();
}

void sdl_shutdown(void) {
    if (s_texture)  SDL_DestroyTexture(s_texture);
    if (s_renderer) SDL_DestroyRenderer(s_renderer);
    if (s_window)   SDL_DestroyWindow(s_window);
    SDL_Quit();
    s_texture  = NULL;
    s_renderer = NULL;
    s_window   = NULL;
}
