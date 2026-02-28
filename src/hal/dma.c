#include "hal/dma.h"
#include "recomp/memory.h"
#include "hal/ppu.h"
#include "hal/apu.h"
#include <string.h>
#include <stdio.h>

SMKDma g_dma;

void dma_init(void) {
    memset(&g_dma, 0, sizeof(g_dma));
}

void dma_write(uint16_t addr, uint8_t val) {
    if (addr < DMA_BASE || addr > 0x437F) return;

    uint8_t ch = (uint8_t)((addr - DMA_BASE) >> 4);
    uint8_t reg = (uint8_t)((addr - DMA_BASE) & 0x0F);

    if (ch >= DMA_CHANNELS) return;

    SMKDmaChannel *c = &g_dma.channels[ch];
    switch (reg) {
    case 0x00: c->ctrl = val; break;
    case 0x01: c->dest = val; break;
    case 0x02: c->src_addr = (c->src_addr & 0xFF00) | val; break;
    case 0x03: c->src_addr = (c->src_addr & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x04: c->src_bank = val; break;
    case 0x05: c->size = (c->size & 0xFF00) | val; break;
    case 0x06: c->size = (c->size & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x07: c->indirect_bank = val; break;
    case 0x08: c->table_addr = (c->table_addr & 0xFF00) | val; break;
    case 0x09: c->table_addr = (c->table_addr & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x0A: c->line_count = val; break;
    default: break;
    }
}

uint8_t dma_read(uint16_t addr) {
    if (addr < DMA_BASE || addr > 0x437F) return 0;

    uint8_t ch = (uint8_t)((addr - DMA_BASE) >> 4);
    uint8_t reg = (uint8_t)((addr - DMA_BASE) & 0x0F);

    if (ch >= DMA_CHANNELS) return 0;

    SMKDmaChannel *c = &g_dma.channels[ch];
    switch (reg) {
    case 0x00: return c->ctrl;
    case 0x01: return c->dest;
    case 0x02: return (uint8_t)(c->src_addr & 0xFF);
    case 0x03: return (uint8_t)(c->src_addr >> 8);
    case 0x04: return c->src_bank;
    case 0x05: return (uint8_t)(c->size & 0xFF);
    case 0x06: return (uint8_t)(c->size >> 8);
    case 0x07: return c->indirect_bank;
    case 0x08: return (uint8_t)(c->table_addr & 0xFF);
    case 0x09: return (uint8_t)(c->table_addr >> 8);
    case 0x0A: return c->line_count;
    default:   return 0;
    }
}

void dma_execute(uint8_t channel_mask) {
    /* Stub: basic DMA transfer for each enabled channel */
    for (int ch = 0; ch < DMA_CHANNELS; ch++) {
        if (!(channel_mask & (1 << ch))) continue;

        SMKDmaChannel *c = &g_dma.channels[ch];
        uint8_t direction = c->ctrl & 0x80;    /* 0 = A->B, 1 = B->A */
        uint8_t mode = c->ctrl & 0x07;          /* Transfer mode */
        uint16_t b_addr = 0x2100 + c->dest;
        uint16_t count = c->size ? c->size : 0x10000;  /* 0 = 65536 */
        uint16_t a_addr = c->src_addr;
        uint8_t  a_bank = c->src_bank;
        int8_t   a_step = (c->ctrl & 0x08) ? 0 : ((c->ctrl & 0x10) ? -1 : 1);

        (void)mode; /* TODO: handle multi-register transfer patterns */

        for (uint32_t i = 0; i < count; i++) {
            if (!direction) {
                /* A-bus -> B-bus */
                uint8_t val = mem_read8(a_bank, a_addr);
                /* Write to B-bus (PPU/APU) via direct register write */
                if (b_addr >= 0x2100 && b_addr <= 0x213F)
                    ppu_write(b_addr, val);
                else if (b_addr >= 0x2140 && b_addr <= 0x2143)
                    apu_write(b_addr, val);
            } else {
                /* B-bus -> A-bus */
                uint8_t val = 0;
                if (b_addr >= 0x2100 && b_addr <= 0x213F)
                    val = ppu_read(b_addr);
                else if (b_addr >= 0x2140 && b_addr <= 0x2143)
                    val = apu_read(b_addr);
                mem_write8(a_bank, a_addr, val);
            }
            a_addr = (uint16_t)(a_addr + a_step);
        }

        c->size = 0;
    }
}
