#include "platform/input.h"
#include <SDL.h>

static uint16_t s_joypad_state = 0;

void input_update(void) {
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint16_t state = 0;

    /* D-pad */
    if (keys[SDL_SCANCODE_UP])     state |= SNES_BTN_UP;
    if (keys[SDL_SCANCODE_DOWN])   state |= SNES_BTN_DOWN;
    if (keys[SDL_SCANCODE_LEFT])   state |= SNES_BTN_LEFT;
    if (keys[SDL_SCANCODE_RIGHT])  state |= SNES_BTN_RIGHT;

    /* Face buttons: Z=B, X=Y, A=A, S=X */
    if (keys[SDL_SCANCODE_Z])      state |= SNES_BTN_B;
    if (keys[SDL_SCANCODE_X])      state |= SNES_BTN_Y;
    if (keys[SDL_SCANCODE_A])      state |= SNES_BTN_A;
    if (keys[SDL_SCANCODE_S])      state |= SNES_BTN_X;

    /* Shoulder buttons */
    if (keys[SDL_SCANCODE_Q])      state |= SNES_BTN_L;
    if (keys[SDL_SCANCODE_W])      state |= SNES_BTN_R;

    /* Start / Select */
    if (keys[SDL_SCANCODE_RETURN]) state |= SNES_BTN_START;
    if (keys[SDL_SCANCODE_RSHIFT]) state |= SNES_BTN_SELECT;

    s_joypad_state = state;
}

uint16_t input_read_joypad(int port) {
    if (port == 0) return s_joypad_state;
    return 0;  /* Only player 1 mapped for now */
}
