#ifndef SMK_SDL_BACKEND_H
#define SMK_SDL_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

/* Window defaults: 256x224 scaled 3x */
#define SMK_WINDOW_WIDTH   768
#define SMK_WINDOW_HEIGHT  672
#define SMK_RENDER_WIDTH   256
#define SMK_RENDER_HEIGHT  224

/* Initialize SDL2 window, renderer, and streaming texture */
bool sdl_init(void);

/* Upload framebuffer (256x224 RGBX8888) to texture and present */
void sdl_present_frame(const uint32_t *framebuffer);

/* Poll SDL events. Returns false if quit was requested. */
bool sdl_poll_events(void);

/* Wait to maintain ~60.098 Hz NTSC frame timing */
void sdl_frame_sync(void);

/* Clean shutdown */
void sdl_shutdown(void);

#endif /* SMK_SDL_BACKEND_H */
