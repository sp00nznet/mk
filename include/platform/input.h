#ifndef SMK_INPUT_H
#define SMK_INPUT_H

#include <stdint.h>

/* SNES joypad button bits (active high) */
#define SNES_BTN_B       0x8000
#define SNES_BTN_Y       0x4000
#define SNES_BTN_SELECT  0x2000
#define SNES_BTN_START   0x1000
#define SNES_BTN_UP      0x0800
#define SNES_BTN_DOWN    0x0400
#define SNES_BTN_LEFT    0x0200
#define SNES_BTN_RIGHT   0x0100
#define SNES_BTN_A       0x0080
#define SNES_BTN_X       0x0040
#define SNES_BTN_L       0x0020
#define SNES_BTN_R       0x0010

/* Update internal key state from SDL event loop */
void input_update(void);

/* Read joypad state for a port (0-3). Returns SNES-format 16-bit value. */
uint16_t input_read_joypad(int port);

#endif /* SMK_INPUT_H */
