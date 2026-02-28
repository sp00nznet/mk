#ifndef SMK_MEMORY_H
#define SMK_MEMORY_H

#include <stdint.h>
#include <stdbool.h>

#define SMK_ROM_SIZE   (512 * 1024)   /* 512 KB */
#define SMK_WRAM_SIZE  (128 * 1024)   /* 128 KB */
#define SMK_SRAM_SIZE  (2   * 1024)   /* 2 KB   */

typedef struct SMKMemory {
    uint8_t rom[SMK_ROM_SIZE];
    uint8_t wram[SMK_WRAM_SIZE];
    uint8_t sram[SMK_SRAM_SIZE];
    bool    rom_loaded;
} SMKMemory;

extern SMKMemory g_mem;

/* Initialize memory (zero-fill RAM) */
void mem_init(void);

/* Load a .sfc ROM file, returns true on success */
bool mem_load_rom(const char *path);

/* 24-bit address space reads/writes */
uint8_t  mem_read8(uint8_t bank, uint16_t addr);
void     mem_write8(uint8_t bank, uint16_t addr, uint8_t val);
uint16_t mem_read16(uint8_t bank, uint16_t addr);
void     mem_write16(uint8_t bank, uint16_t addr, uint16_t val);

#endif /* SMK_MEMORY_H */
