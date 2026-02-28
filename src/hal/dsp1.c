#include "hal/dsp1.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

SMKDsp1 g_dsp1;

/* Parameter counts for each DSP-1 command (by opcode >> 1) */
static const int dsp1_param_counts[32] = {
    /* $00 */ 2, /* $02 */ 1, /* $04 */ 2, /* $06 */ 1,
    /* $08 */ 0, /* $0A */ 1, /* $0C */ 0, /* $0E */ 1,
    /* $10 */ 2, /* $12 */ 0, /* $14 */ 1, /* $16 */ 0,
    /* $18 */ 1, /* $1A */ 3, /* $1C */ 1, /* $1E */ 3,
    /* $20 */ 0, /* $22 */ 1, /* $24 */ 0, /* $26 */ 0,
    /* $28 */ 3, /* $2A */ 3, /* $2C */ 0, /* $2E */ 0,
    /* $30 */ 0, /* $32 */ 0, /* $34 */ 0, /* $36 */ 0,
    /* $38 */ 0, /* $3A */ 0, /* $3C */ 0, /* $3E */ 0,
};

static const int dsp1_result_counts[32] = {
    /* $00 */ 2, /* $02 */ 1, /* $04 */ 2, /* $06 */ 1,
    /* $08 */ 3, /* $0A */ 1, /* $0C */ 3, /* $0E */ 1,
    /* $10 */ 2, /* $12 */ 0, /* $14 */ 1, /* $16 */ 3,
    /* $18 */ 1, /* $1A */ 1, /* $1C */ 1, /* $1E */ 1,
    /* $20 */ 1, /* $22 */ 1, /* $24 */ 0, /* $26 */ 0,
    /* $28 */ 3, /* $2A */ 3, /* $2C */ 0, /* $2E */ 0,
    /* $30 */ 0, /* $32 */ 0, /* $34 */ 0, /* $36 */ 0,
    /* $38 */ 0, /* $3A */ 0, /* $3C */ 0, /* $3E */ 0,
};

void dsp1_init(void) {
    memset(&g_dsp1, 0, sizeof(g_dsp1));
    g_dsp1.status = 0x00;  /* Ready */
}

static void dsp1_execute_command(void) {
    /* Stub: just zero out results */
    printf("DSP-1: command $%02X with %d params\n",
           g_dsp1.command, g_dsp1.param_count);
    memset(g_dsp1.result, 0, sizeof(g_dsp1.result));

    uint8_t cmd_idx = (g_dsp1.command >> 1) & 0x1F;
    g_dsp1.result_count = dsp1_result_counts[cmd_idx];
    g_dsp1.result_index = 0;
    g_dsp1.status = 0x80;  /* Results ready */
}

void dsp1_write(uint16_t addr, uint8_t val) {
    if (addr >= DSP1_STATUS_ADDR) {
        /* Status port writes are ignored */
        return;
    }

    /* Data port write */
    if (!g_dsp1.waiting_for_params) {
        /* First write: command byte */
        g_dsp1.command = val;
        uint8_t cmd_idx = (val >> 1) & 0x1F;
        g_dsp1.param_count = dsp1_param_counts[cmd_idx];
        g_dsp1.param_index = 0;

        if (g_dsp1.param_count > 0) {
            g_dsp1.waiting_for_params = true;
        } else {
            dsp1_execute_command();
        }
    } else {
        /* Parameter data (16-bit little-endian, received byte-by-byte) */
        int word_idx = g_dsp1.param_index / 2;
        if (g_dsp1.param_index & 1) {
            g_dsp1.params[word_idx] = (int16_t)(
                (g_dsp1.params[word_idx] & 0x00FF) | ((uint16_t)val << 8)
            );
        } else {
            g_dsp1.params[word_idx] = (int16_t)val;
        }
        g_dsp1.param_index++;

        if (g_dsp1.param_index >= g_dsp1.param_count * 2) {
            g_dsp1.waiting_for_params = false;
            dsp1_execute_command();
        }
    }
}

uint8_t dsp1_read(uint16_t addr) {
    if (addr >= DSP1_STATUS_ADDR) {
        return g_dsp1.status;
    }

    /* Data port read: return result bytes */
    if (g_dsp1.result_count == 0) return 0;

    int word_idx = g_dsp1.result_index / 2;
    uint8_t val;
    if (g_dsp1.result_index & 1) {
        val = (uint8_t)(g_dsp1.result[word_idx] >> 8);
    } else {
        val = (uint8_t)(g_dsp1.result[word_idx] & 0xFF);
    }
    g_dsp1.result_index++;

    if (g_dsp1.result_index >= g_dsp1.result_count * 2) {
        g_dsp1.status = 0x00;  /* Ready for next command */
    }
    return val;
}
