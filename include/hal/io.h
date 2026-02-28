#ifndef SMK_IO_H
#define SMK_IO_H

#include <stdint.h>
#include <stdbool.h>

/* CPU I/O registers ($4200-$42FF) */
#define IO_NMITIMEN  0x4200
#define IO_WRIO      0x4201
#define IO_WRMPYA    0x4202
#define IO_WRMPYB    0x4203
#define IO_WRDIVL    0x4204
#define IO_WRDIVH    0x4205
#define IO_WRDIVB    0x4206
#define IO_HTIMEL    0x4207
#define IO_HTIMEH    0x4208
#define IO_VTIMEL    0x4209
#define IO_VTIMEH    0x420A
#define IO_MDMAEN    0x420B
#define IO_HDMAEN    0x420C
#define IO_MEMSEL    0x420D
#define IO_RDNMI     0x4210
#define IO_TIMEUP    0x4211
#define IO_HVBJOY    0x4212
#define IO_RDIO      0x4213
#define IO_RDDIVL    0x4214
#define IO_RDDIVH    0x4215
#define IO_RDMPYL    0x4216
#define IO_RDMPYH    0x4217
#define IO_JOY1L     0x4218
#define IO_JOY1H     0x4219
#define IO_JOY2L     0x421A
#define IO_JOY2H     0x421B
#define IO_JOY3L     0x421C
#define IO_JOY3H     0x421D
#define IO_JOY4L     0x421E
#define IO_JOY4H     0x421F

typedef struct SMKIo {
    /* NMI/IRQ control */
    uint8_t  nmitimen;
    uint8_t  wrio;
    uint16_t htime;
    uint16_t vtime;
    uint8_t  memsel;

    /* ALU */
    uint8_t  wrmpya;
    uint8_t  wrmpyb;
    uint16_t wrdiv;
    uint8_t  wrdivb;
    uint16_t rddiv;
    uint16_t rdmpy;

    /* Status */
    uint8_t  rdnmi;
    uint8_t  timeup;
    uint8_t  hvbjoy;
    uint8_t  rdio;

    /* Joypad auto-read results */
    uint16_t joy[4];

    /* Internal */
    bool     nmi_pending;
    bool     irq_pending;
    bool     auto_joypad_read;
} SMKIo;

extern SMKIo g_io;

void    io_init(void);
void    io_write(uint16_t addr, uint8_t val);
uint8_t io_read(uint16_t addr);

#endif /* SMK_IO_H */
