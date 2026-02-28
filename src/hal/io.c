#include "hal/io.h"
#include "hal/dma.h"
#include "platform/input.h"
#include <string.h>

SMKIo g_io;

void io_init(void) {
    memset(&g_io, 0, sizeof(g_io));
    g_io.rdnmi = 0x02;    /* CPU version 2 */
    g_io.hvbjoy = 0x00;
}

void io_write(uint16_t addr, uint8_t val) {
    switch (addr) {
    case IO_NMITIMEN:
        g_io.nmitimen = val;
        g_io.auto_joypad_read = (val & 0x01) != 0;
        break;
    case IO_WRIO:    g_io.wrio = val; break;
    case IO_WRMPYA:  g_io.wrmpya = val; break;
    case IO_WRMPYB:
        g_io.wrmpyb = val;
        /* Hardware multiply: result available immediately in recomp */
        g_io.rdmpy = (uint16_t)g_io.wrmpya * (uint16_t)val;
        break;
    case IO_WRDIVL:  g_io.wrdiv = (g_io.wrdiv & 0xFF00) | val; break;
    case IO_WRDIVH:  g_io.wrdiv = (g_io.wrdiv & 0x00FF) | ((uint16_t)val << 8); break;
    case IO_WRDIVB:
        g_io.wrdivb = val;
        /* Hardware divide: result available immediately in recomp */
        if (val != 0) {
            g_io.rddiv = g_io.wrdiv / val;
            g_io.rdmpy = g_io.wrdiv % val;
        } else {
            g_io.rddiv = 0xFFFF;
            g_io.rdmpy = g_io.wrdiv;
        }
        break;
    case IO_HTIMEL: g_io.htime = (g_io.htime & 0xFF00) | val; break;
    case IO_HTIMEH: g_io.htime = (g_io.htime & 0x00FF) | ((uint16_t)(val & 0x01) << 8); break;
    case IO_VTIMEL: g_io.vtime = (g_io.vtime & 0xFF00) | val; break;
    case IO_VTIMEH: g_io.vtime = (g_io.vtime & 0x00FF) | ((uint16_t)(val & 0x01) << 8); break;
    case IO_MDMAEN:
        dma_execute(val);
        break;
    case IO_HDMAEN:
        g_dma.hdma_enable = val;
        break;
    case IO_MEMSEL: g_io.memsel = val; break;
    default:
        break;
    }
}

uint8_t io_read(uint16_t addr) {
    switch (addr) {
    case IO_RDNMI: {
        uint8_t val = g_io.rdnmi;
        g_io.rdnmi &= 0x7F;  /* Reading clears NMI flag (bit 7) */
        return val;
    }
    case IO_TIMEUP: {
        uint8_t val = g_io.timeup;
        g_io.timeup = 0;     /* Reading clears IRQ flag */
        return val;
    }
    case IO_HVBJOY: return g_io.hvbjoy;
    case IO_RDIO:   return g_io.rdio;
    case IO_RDDIVL: return (uint8_t)(g_io.rddiv & 0xFF);
    case IO_RDDIVH: return (uint8_t)(g_io.rddiv >> 8);
    case IO_RDMPYL: return (uint8_t)(g_io.rdmpy & 0xFF);
    case IO_RDMPYH: return (uint8_t)(g_io.rdmpy >> 8);
    case IO_JOY1L:  return (uint8_t)(g_io.joy[0] & 0xFF);
    case IO_JOY1H:  return (uint8_t)(g_io.joy[0] >> 8);
    case IO_JOY2L:  return (uint8_t)(g_io.joy[1] & 0xFF);
    case IO_JOY2H:  return (uint8_t)(g_io.joy[1] >> 8);
    case IO_JOY3L:  return (uint8_t)(g_io.joy[2] & 0xFF);
    case IO_JOY3H:  return (uint8_t)(g_io.joy[2] >> 8);
    case IO_JOY4L:  return (uint8_t)(g_io.joy[3] & 0xFF);
    case IO_JOY4H:  return (uint8_t)(g_io.joy[3] >> 8);
    default:
        return 0;
    }
}
