#ifndef SMK_PPU_H
#define SMK_PPU_H

#include <stdint.h>
#include <stdbool.h>

/* PPU register addresses */
#define PPU_INIDISP   0x2100
#define PPU_OBSEL     0x2101
#define PPU_OAMADDL   0x2102
#define PPU_OAMADDH   0x2103
#define PPU_OAMDATA   0x2104
#define PPU_BGMODE    0x2105
#define PPU_MOSAIC    0x2106
#define PPU_BG1SC     0x2107
#define PPU_BG2SC     0x2108
#define PPU_BG3SC     0x2109
#define PPU_BG4SC     0x210A
#define PPU_BG12NBA   0x210B
#define PPU_BG34NBA   0x210C
#define PPU_BG1HOFS   0x210D
#define PPU_BG1VOFS   0x210E
#define PPU_BG2HOFS   0x210F
#define PPU_BG2VOFS   0x2110
#define PPU_BG3HOFS   0x2111
#define PPU_BG3VOFS   0x2112
#define PPU_BG4HOFS   0x2113
#define PPU_BG4VOFS   0x2114
#define PPU_VMAIN     0x2115
#define PPU_VMADDL    0x2116
#define PPU_VMADDH    0x2117
#define PPU_VMDATAL   0x2118
#define PPU_VMDATAH   0x2119
#define PPU_M7SEL     0x211A
#define PPU_M7A       0x211B
#define PPU_M7B       0x211C
#define PPU_M7C       0x211D
#define PPU_M7D       0x211E
#define PPU_M7X       0x211F
#define PPU_M7Y       0x2120
#define PPU_CGADD     0x2121
#define PPU_CGDATA    0x2122
#define PPU_W12SEL    0x2123
#define PPU_W34SEL    0x2124
#define PPU_WOBJSEL   0x2125
#define PPU_WH0       0x2126
#define PPU_WH1       0x2127
#define PPU_WH2       0x2128
#define PPU_WH3       0x2129
#define PPU_WBGLOG    0x212A
#define PPU_WOBJLOG   0x212B
#define PPU_TM        0x212C
#define PPU_TS        0x212D
#define PPU_TMW       0x212E
#define PPU_TSW       0x212F
#define PPU_CGWSEL    0x2130
#define PPU_CGADSUB   0x2131
#define PPU_COLDATA   0x2132
#define PPU_SETINI    0x2133

/* PPU read registers */
#define PPU_MPYL      0x2134
#define PPU_MPYM      0x2135
#define PPU_MPYH      0x2136
#define PPU_SLHV      0x2137
#define PPU_RDOAM     0x2138
#define PPU_RDVRAML   0x2139
#define PPU_RDVRAMH   0x213A
#define PPU_RDCGRAM   0x213B
#define PPU_OPHCT     0x213C
#define PPU_OPVCT     0x213D
#define PPU_STAT77    0x213E
#define PPU_STAT78    0x213F

#define PPU_VRAM_SIZE    (64 * 1024)
#define PPU_CGRAM_SIZE   512
#define PPU_OAM_SIZE     544
#define PPU_FB_WIDTH     256
#define PPU_FB_HEIGHT    240

typedef struct SMKPpu {
    uint8_t  vram[PPU_VRAM_SIZE];
    uint8_t  cgram[PPU_CGRAM_SIZE];
    uint8_t  oam[PPU_OAM_SIZE];

    /* Framebuffer: RGBX8888, 256x240 */
    uint32_t framebuffer[PPU_FB_WIDTH * PPU_FB_HEIGHT];

    /* Registers (shadow copies) */
    uint8_t  inidisp;
    uint8_t  obsel;
    uint16_t oam_addr;
    uint8_t  bgmode;
    uint8_t  mosaic;
    uint8_t  bgsc[4];
    uint8_t  bgnba[2];
    uint16_t bg_hofs[4];
    uint16_t bg_vofs[4];
    uint8_t  vmain;
    uint16_t vm_addr;
    uint16_t vm_prefetch;
    uint8_t  m7sel;
    int16_t  m7a, m7b, m7c, m7d;
    int16_t  m7x, m7y;
    uint8_t  cg_addr;
    bool     cg_latch;
    uint8_t  cg_low_byte;
    uint8_t  tm, ts;
    uint8_t  cgwsel, cgadsub;
    uint8_t  setini;

    /* Internal state */
    uint16_t scanline;
    bool     oam_latch;
    uint8_t  ppu1_open_bus;
    uint8_t  ppu2_open_bus;
} SMKPpu;

extern SMKPpu g_ppu;

void    ppu_init(void);
void    ppu_write(uint16_t addr, uint8_t val);
uint8_t ppu_read(uint16_t addr);
void    ppu_render_scanline(uint16_t line);
void    ppu_end_frame(void);

#endif /* SMK_PPU_H */
