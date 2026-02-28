#ifndef SMK_DMA_H
#define SMK_DMA_H

#include <stdint.h>

/* DMA channel register base: $4300 + (channel * 0x10) */
#define DMA_BASE      0x4300
#define DMA_CHANNELS  8

typedef struct SMKDmaChannel {
    uint8_t  ctrl;        /* $43x0: DMA control */
    uint8_t  dest;        /* $43x1: B-bus destination */
    uint16_t src_addr;    /* $43x2-3: A-bus source address */
    uint8_t  src_bank;    /* $43x4: A-bus source bank */
    uint16_t size;        /* $43x5-6: transfer size / HDMA indirect addr */
    uint8_t  indirect_bank; /* $43x7: HDMA indirect bank */
    uint16_t table_addr;  /* $43x8-9: HDMA table address */
    uint8_t  line_count;  /* $43xA: HDMA line counter */
} SMKDmaChannel;

typedef struct SMKDma {
    SMKDmaChannel channels[DMA_CHANNELS];
    uint8_t hdma_enable;  /* $420C */
} SMKDma;

extern SMKDma g_dma;

void    dma_init(void);
void    dma_write(uint16_t addr, uint8_t val);
uint8_t dma_read(uint16_t addr);
void    dma_execute(uint8_t channel_mask);  /* Triggered by write to $420B */

#endif /* SMK_DMA_H */
