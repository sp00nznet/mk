#include "hal/ppu.h"
#include <string.h>

SMKPpu g_ppu;

void ppu_init(void) {
    memset(&g_ppu, 0, sizeof(g_ppu));
    g_ppu.inidisp = 0x80;  /* Force blank on reset */
}

void ppu_write(uint16_t addr, uint8_t val) {
    switch (addr) {
    case PPU_INIDISP: g_ppu.inidisp = val; break;
    case PPU_OBSEL:   g_ppu.obsel = val; break;
    case PPU_OAMADDL: g_ppu.oam_addr = (g_ppu.oam_addr & 0xFF00) | val; break;
    case PPU_OAMADDH: g_ppu.oam_addr = (g_ppu.oam_addr & 0x00FF) | ((uint16_t)(val & 0x01) << 8); break;
    case PPU_OAMDATA:
        if (g_ppu.oam_addr < PPU_OAM_SIZE) {
            g_ppu.oam[g_ppu.oam_addr] = val;
        }
        g_ppu.oam_addr = (g_ppu.oam_addr + 1) & 0x03FF;
        break;
    case PPU_BGMODE: g_ppu.bgmode = val; break;
    case PPU_MOSAIC: g_ppu.mosaic = val; break;
    case PPU_BG1SC: g_ppu.bgsc[0] = val; break;
    case PPU_BG2SC: g_ppu.bgsc[1] = val; break;
    case PPU_BG3SC: g_ppu.bgsc[2] = val; break;
    case PPU_BG4SC: g_ppu.bgsc[3] = val; break;
    case PPU_BG12NBA: g_ppu.bgnba[0] = val; break;
    case PPU_BG34NBA: g_ppu.bgnba[1] = val; break;
    case PPU_BG1HOFS: g_ppu.bg_hofs[0] = val | ((uint16_t)(val & 0x04) ? 0xFF00 : 0); break;
    case PPU_BG1VOFS: g_ppu.bg_vofs[0] = val | ((uint16_t)(val & 0x04) ? 0xFF00 : 0); break;
    case PPU_BG2HOFS: g_ppu.bg_hofs[1] = val; break;
    case PPU_BG2VOFS: g_ppu.bg_vofs[1] = val; break;
    case PPU_BG3HOFS: g_ppu.bg_hofs[2] = val; break;
    case PPU_BG3VOFS: g_ppu.bg_vofs[2] = val; break;
    case PPU_BG4HOFS: g_ppu.bg_hofs[3] = val; break;
    case PPU_BG4VOFS: g_ppu.bg_vofs[3] = val; break;
    case PPU_VMAIN: g_ppu.vmain = val; break;
    case PPU_VMADDL: g_ppu.vm_addr = (g_ppu.vm_addr & 0xFF00) | val; break;
    case PPU_VMADDH: g_ppu.vm_addr = (g_ppu.vm_addr & 0x00FF) | ((uint16_t)val << 8); break;
    case PPU_VMDATAL: {
        uint16_t vram_addr = g_ppu.vm_addr << 1;
        if (vram_addr < PPU_VRAM_SIZE)
            g_ppu.vram[vram_addr] = val;
        if (!(g_ppu.vmain & 0x80))
            g_ppu.vm_addr += (g_ppu.vmain & 0x03) == 0 ? 1 : (g_ppu.vmain & 0x03) == 1 ? 32 : 128;
        break;
    }
    case PPU_VMDATAH: {
        uint16_t vram_addr = (g_ppu.vm_addr << 1) + 1;
        if (vram_addr < PPU_VRAM_SIZE)
            g_ppu.vram[vram_addr] = val;
        if (g_ppu.vmain & 0x80)
            g_ppu.vm_addr += (g_ppu.vmain & 0x03) == 0 ? 1 : (g_ppu.vmain & 0x03) == 1 ? 32 : 128;
        break;
    }
    case PPU_M7SEL: g_ppu.m7sel = val; break;
    case PPU_M7A: g_ppu.m7a = (int16_t)val; break;
    case PPU_M7B: g_ppu.m7b = (int16_t)val; break;
    case PPU_M7C: g_ppu.m7c = (int16_t)val; break;
    case PPU_M7D: g_ppu.m7d = (int16_t)val; break;
    case PPU_M7X: g_ppu.m7x = (int16_t)val; break;
    case PPU_M7Y: g_ppu.m7y = (int16_t)val; break;
    case PPU_CGADD:
        g_ppu.cg_addr = val;
        g_ppu.cg_latch = false;
        break;
    case PPU_CGDATA:
        if (!g_ppu.cg_latch) {
            g_ppu.cg_low_byte = val;
            g_ppu.cg_latch = true;
        } else {
            uint16_t offset = (uint16_t)g_ppu.cg_addr * 2;
            if (offset < PPU_CGRAM_SIZE - 1) {
                g_ppu.cgram[offset]     = g_ppu.cg_low_byte;
                g_ppu.cgram[offset + 1] = val & 0x7F;
            }
            g_ppu.cg_addr++;
            g_ppu.cg_latch = false;
        }
        break;
    case PPU_TM: g_ppu.tm = val; break;
    case PPU_TS: g_ppu.ts = val; break;
    case PPU_CGWSEL: g_ppu.cgwsel = val; break;
    case PPU_CGADSUB: g_ppu.cgadsub = val; break;
    case PPU_SETINI: g_ppu.setini = val; break;
    default:
        break;
    }
}

uint8_t ppu_read(uint16_t addr) {
    switch (addr) {
    case PPU_RDOAM:
        if (g_ppu.oam_addr < PPU_OAM_SIZE) {
            uint8_t val = g_ppu.oam[g_ppu.oam_addr];
            g_ppu.oam_addr = (g_ppu.oam_addr + 1) & 0x03FF;
            return val;
        }
        return 0;
    case PPU_RDVRAML: {
        uint16_t vram_addr = g_ppu.vm_addr << 1;
        uint8_t val = (vram_addr < PPU_VRAM_SIZE) ? g_ppu.vram[vram_addr] : 0;
        if (!(g_ppu.vmain & 0x80))
            g_ppu.vm_addr += (g_ppu.vmain & 0x03) == 0 ? 1 : (g_ppu.vmain & 0x03) == 1 ? 32 : 128;
        return val;
    }
    case PPU_RDVRAMH: {
        uint16_t vram_addr = (g_ppu.vm_addr << 1) + 1;
        uint8_t val = (vram_addr < PPU_VRAM_SIZE) ? g_ppu.vram[vram_addr] : 0;
        if (g_ppu.vmain & 0x80)
            g_ppu.vm_addr += (g_ppu.vmain & 0x03) == 0 ? 1 : (g_ppu.vmain & 0x03) == 1 ? 32 : 128;
        return val;
    }
    case PPU_STAT77: return 0x01;  /* PPU1 version */
    case PPU_STAT78: return 0x01;  /* PPU2 version */
    default:
        return g_ppu.ppu1_open_bus;
    }
}

void ppu_render_scanline(uint16_t line) {
    /* Stub: fill scanline with backdrop color */
    if (line >= PPU_FB_HEIGHT) return;

    uint32_t backdrop = 0x00000000;  /* Black */
    if (!(g_ppu.inidisp & 0x80) && g_ppu.cgram[0] != 0) {
        /* Convert 15-bit BGR to RGBX8888 */
        uint16_t col = g_ppu.cgram[0] | ((uint16_t)g_ppu.cgram[1] << 8);
        uint8_t r = (col & 0x1F) << 3;
        uint8_t g = ((col >> 5) & 0x1F) << 3;
        uint8_t b = ((col >> 10) & 0x1F) << 3;
        backdrop = (r << 16) | (g << 8) | b;
    }

    uint32_t *row = &g_ppu.framebuffer[line * PPU_FB_WIDTH];
    for (int x = 0; x < PPU_FB_WIDTH; x++) {
        row[x] = backdrop;
    }
}

void ppu_end_frame(void) {
    /* Render all visible scanlines */
    for (uint16_t line = 0; line < 224; line++) {
        ppu_render_scanline(line);
    }
}
