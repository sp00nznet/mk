#include "recomp/memory.h"
#include "hal/ppu.h"
#include "hal/apu.h"
#include "hal/dma.h"
#include "hal/dsp1.h"
#include "hal/io.h"
#include <stdio.h>
#include <string.h>

SMKMemory g_mem;

void mem_init(void) {
    memset(&g_mem, 0, sizeof(g_mem));
}

bool mem_load_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open ROM: %s\n", path);
        return false;
    }

    /* Check for optional 512-byte copier header */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    long header_offset = 0;
    if ((size % 1024) == 512) {
        header_offset = 512;
        size -= 512;
    }

    if (size != SMK_ROM_SIZE) {
        fprintf(stderr, "Unexpected ROM size: %ld bytes (expected %d)\n",
                size, SMK_ROM_SIZE);
        fclose(f);
        return false;
    }

    if (header_offset > 0) {
        fseek(f, header_offset, SEEK_SET);
    }

    size_t read = fread(g_mem.rom, 1, SMK_ROM_SIZE, f);
    fclose(f);

    if (read != SMK_ROM_SIZE) {
        fprintf(stderr, "Failed to read full ROM (%zu bytes read)\n", read);
        return false;
    }

    /* Verify HiROM header: internal name at $FFC0 (file offset $FFC0) */
    char name[22];
    memcpy(name, &g_mem.rom[0xFFC0], 21);
    name[21] = '\0';
    printf("ROM internal name: %s\n", name);

    /* Verify checksum complement pair at $FFDC-$FFDF */
    uint16_t checksum_compl = g_mem.rom[0xFFDC] | (g_mem.rom[0xFFDD] << 8);
    uint16_t checksum       = g_mem.rom[0xFFDE] | (g_mem.rom[0xFFDF] << 8);
    if ((uint16_t)(checksum + checksum_compl) != 0xFFFF) {
        fprintf(stderr, "Warning: checksum pair invalid (sum=%04X)\n",
                checksum + checksum_compl);
    } else {
        printf("ROM checksum verified: %04X (complement %04X)\n",
               checksum, checksum_compl);
    }

    g_mem.rom_loaded = true;
    return true;
}

/*
 * HiROM address mapping:
 *
 * Banks 00-3F, 80-BF:
 *   $0000-$1FFF  WRAM mirror (first 8KB)
 *   $2100-$213F  PPU registers
 *   $2140-$2143  APU I/O
 *   $4200-$42FF  CPU I/O
 *   $4300-$437F  DMA registers
 *   $6000-$7FFF  DSP-1 / SRAM
 *   $8000-$FFFF  ROM (mapped from bank * 0x10000 + addr, masked into 512KB)
 *
 * Banks 40-7D:
 *   $0000-$FFFF  ROM (full 64KB per bank, mapped as (bank-0x40)*0x10000 + addr)
 *
 * Banks 7E-7F:
 *   $0000-$FFFF  WRAM (128KB total)
 *
 * Banks C0-FF mirror 40-7F for ROM access.
 */
uint8_t mem_read8(uint8_t bank, uint16_t addr) {
    uint8_t effective_bank = bank & 0x7F;  /* Mirror $80+ down */

    /* Banks 7E-7F: WRAM */
    if (bank == 0x7E || bank == 0x7F) {
        uint32_t wram_offset = ((uint32_t)(bank - 0x7E) << 16) | addr;
        return g_mem.wram[wram_offset & (SMK_WRAM_SIZE - 1)];
    }

    /* Banks 40-7D (or C0-FF via mirror): full ROM access */
    if (effective_bank >= 0x40 && effective_bank <= 0x7D) {
        uint32_t rom_offset = ((uint32_t)(effective_bank - 0x40) << 16) | addr;
        return g_mem.rom[rom_offset & (SMK_ROM_SIZE - 1)];
    }

    /* Banks 00-3F, 80-BF */
    if (addr < 0x2000) {
        /* WRAM mirror (first 8KB) */
        return g_mem.wram[addr];
    }
    if (addr >= 0x2100 && addr <= 0x213F) {
        return ppu_read(addr);
    }
    if (addr >= 0x2140 && addr <= 0x2143) {
        return apu_read(addr);
    }
    if (addr >= 0x4200 && addr <= 0x42FF) {
        return io_read(addr);
    }
    if (addr >= 0x4300 && addr <= 0x437F) {
        return dma_read(addr);
    }
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        /* DSP-1 in banks 00-1F (status at $7000+, data at $6000+) */
        if (effective_bank < 0x20) {
            return dsp1_read(addr);
        }
        /* SRAM at $6000-$7FFF in banks 20-3F */
        uint16_t sram_offset = addr - 0x6000;
        return g_mem.sram[sram_offset & (SMK_SRAM_SIZE - 1)];
    }
    if (addr >= 0x8000) {
        /* ROM: HiROM maps bank:8000-FFFF */
        uint32_t rom_offset = ((uint32_t)effective_bank << 16) | addr;
        return g_mem.rom[rom_offset & (SMK_ROM_SIZE - 1)];
    }

    /* Unmapped / open bus */
    return 0;
}

void mem_write8(uint8_t bank, uint16_t addr, uint8_t val) {
    uint8_t effective_bank = bank & 0x7F;

    /* Banks 7E-7F: WRAM */
    if (bank == 0x7E || bank == 0x7F) {
        uint32_t wram_offset = ((uint32_t)(bank - 0x7E) << 16) | addr;
        g_mem.wram[wram_offset & (SMK_WRAM_SIZE - 1)] = val;
        return;
    }

    /* Banks 40-7D: ROM (writes ignored) */
    if (effective_bank >= 0x40 && effective_bank <= 0x7D) {
        return;
    }

    /* Banks 00-3F, 80-BF */
    if (addr < 0x2000) {
        g_mem.wram[addr] = val;
        return;
    }
    if (addr >= 0x2100 && addr <= 0x213F) {
        ppu_write(addr, val);
        return;
    }
    if (addr >= 0x2140 && addr <= 0x2143) {
        apu_write(addr, val);
        return;
    }
    if (addr >= 0x4200 && addr <= 0x42FF) {
        io_write(addr, val);
        return;
    }
    if (addr >= 0x4300 && addr <= 0x437F) {
        dma_write(addr, val);
        return;
    }
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (effective_bank < 0x20) {
            dsp1_write(addr, val);
            return;
        }
        uint16_t sram_offset = addr - 0x6000;
        g_mem.sram[sram_offset & (SMK_SRAM_SIZE - 1)] = val;
        return;
    }
    /* ROM region ($8000+) or unmapped: writes ignored */
}

uint16_t mem_read16(uint8_t bank, uint16_t addr) {
    uint8_t lo = mem_read8(bank, addr);
    /* Handle bank-boundary wrapping */
    uint16_t next_addr = addr + 1;
    uint8_t next_bank = bank;
    if (next_addr == 0x0000) {
        next_bank++;
    }
    uint8_t hi = mem_read8(next_bank, next_addr);
    return (uint16_t)(lo | (hi << 8));
}

void mem_write16(uint8_t bank, uint16_t addr, uint16_t val) {
    mem_write8(bank, addr, (uint8_t)(val & 0xFF));
    uint16_t next_addr = addr + 1;
    uint8_t next_bank = bank;
    if (next_addr == 0x0000) {
        next_bank++;
    }
    mem_write8(next_bank, next_addr, (uint8_t)(val >> 8));
}
