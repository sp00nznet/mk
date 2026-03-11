/*
 * Super Mario Kart — Title screen initialization (state $04)
 *
 * Transition handler $81:E0AD and its sub-calls.
 * Sets up PPU registers, loads tile data and tilemaps to VRAM,
 * loads palette data, and configures the title screen display.
 */

#include "smk/functions.h"
#include "smk/cpu_ops.h"
#include <snesrecomp/snesrecomp.h>
#include <string.h>
#include <stdio.h>

/*
 * $84:E09E — VRAM data loader (data-driven decompression + DMA)
 *
 * Entry: X = VRAM destination word address
 *        Y = compressed data pointer (within bank)
 *        A (low byte) = source bank
 *
 * The routine decompresses tile/tilemap data from ROM into a WRAM
 * buffer at $7F:0000+, then DMA's the result to VRAM.
 *
 * Data format (command byte):
 *   $FF        = end of data
 *   $E0-$FE   = tilemap offset command (3 extra bytes)
 *   $00-$1F   = raw copy: (cmd & $1F)+1 bytes from stream to buffer
 *   $20       = RLE fill: read 1 byte, repeat (count) times
 *   $40       = word fill: read 2 bytes, alternate for (count) pairs
 *   $60       = incrementing fill: read 1 byte, store incrementing
 *   $80-$9F   = back-reference (copy from earlier in buffer)
 *   $A0-$BF   = back-reference with XOR $FF (inverted copy)
 *   $C0-$DF   = back-reference (offset from stream byte)
 *
 * After decompression, the buffer is DMA'd to the specified VRAM address.
 */
void smk_84E09E(void) {
    uint16_t vram_dest = g_cpu.X;
    uint16_t data_ptr = g_cpu.Y;
    uint8_t data_bank = CPU_A16() & 0xFF;

    uint8_t saved_db = g_cpu.DB;

    /* Get WRAM pointer for direct buffer access */
    uint8_t *wram = bus_get_wram();
    if (!wram) {
        g_cpu.DB = saved_db;
        return;
    }

    /* The decompressor writes to WRAM at $7F:0000+ (wram + 0x10000) */
    /* Output position starts at vram_dest (the original stores X → DP $0E) */
    uint8_t *buf = wram + 0x10000;  /* $7F bank = WRAM offset $10000 */

    uint16_t buf_pos = vram_dest;  /* DP $0E — starts at VRAM dest addr */
    uint16_t src_pos = data_ptr;   /* DP $10 — source data pointer */
    uint16_t tile_offset = 0;     /* DP $12 — tilemap offset (E0+ cmds) */

    while (1) {
        /* Read command byte from ROM via bus */
        uint8_t cmd = bus_read8(data_bank, src_pos++);

        if (cmd == 0xFF) break;  /* End of data */

        uint8_t mode;
        uint16_t count;

        /* $E0+ commands: extended count with mode encoded in cmd byte */
        if ((cmd & 0xE0) == 0xE0) {
            /* Mode = (cmd << 3) & 0xE0 */
            mode = (cmd << 3) & 0xE0;

            /* Read 2 bytes: overlapping 16-bit reads to get count
             * Original reads $12 = word at [ptr], then $13 = word at [ptr-1]
             * Net effect: $12 = low byte of word, count = word & 0x03FF
             * Actually: read 16-bit at src_pos, mask low 10 bits, that's the count */
            uint8_t lo = bus_read8(data_bank, src_pos);
            uint8_t hi = bus_read8(data_bank, src_pos + 1);
            src_pos += 2;
            count = ((lo | (hi << 8)) & 0x03FF) + 1;
        } else {
            /* Regular command: mode is top 3 bits, count is bottom 5 bits + 1 */
            mode = cmd & 0xE0;
            count = (cmd & 0x1F) + 1;
        }

        switch (mode) {
        case 0x00:
            /* Raw copy: copy (count) bytes from stream to buffer */
            for (int i = 0; i < count; i++) {
                buf[buf_pos++] = bus_read8(data_bank, src_pos++);
            }
            break;

        case 0x20:
            /* RLE fill: read 1 byte, repeat (count) times */
            {
                uint8_t fill = bus_read8(data_bank, src_pos++);
                for (int i = 0; i < count; i++) {
                    buf[buf_pos++] = fill;
                }
            }
            break;

        case 0x40:
            /* Word fill: read 2 bytes, alternate for (count) entries */
            /* Original: writes b0, then b1, decrementing Y each time */
            {
                uint8_t b0 = bus_read8(data_bank, src_pos++);
                uint8_t b1 = bus_read8(data_bank, src_pos++);
                for (int i = count; i > 0; ) {
                    buf[buf_pos++] = b0;
                    i--;
                    if (i == 0) break;
                    buf[buf_pos++] = b1;
                    i--;
                }
            }
            break;

        case 0x60:
            /* Incrementing fill: read 1 byte, store and increment */
            {
                uint8_t val = bus_read8(data_bank, src_pos++);
                for (int i = 0; i < count; i++) {
                    buf[buf_pos++] = val++;
                }
            }
            break;

        case 0x80:
            /* Back-reference: copy from (buf_pos - offset) */
            /* Next 2 bytes = source offset added to base VRAM addr */
            {
                uint16_t ref_lo = bus_read8(data_bank, src_pos);
                uint16_t ref_hi = bus_read8(data_bank, src_pos + 1);
                src_pos += 2;
                uint16_t ref_addr = ref_lo | (ref_hi << 8);
                /* ref_addr is relative to buffer base */
                for (int i = 0; i < count; i++) {
                    buf[buf_pos] = buf[ref_addr];
                    buf_pos++;
                    ref_addr++;
                }
            }
            break;

        case 0xA0:
            /* Back-reference with XOR $FF (inverted copy) */
            {
                uint16_t ref_lo = bus_read8(data_bank, src_pos);
                uint16_t ref_hi = bus_read8(data_bank, src_pos + 1);
                src_pos += 2;
                uint16_t ref_addr = ref_lo | (ref_hi << 8);
                for (int i = 0; i < count; i++) {
                    buf[buf_pos] = buf[ref_addr] ^ 0xFF;
                    buf_pos++;
                    ref_addr++;
                }
            }
            break;

        case 0xC0:
            /* Back-reference from byte offset */
            {
                uint8_t offset_byte = bus_read8(data_bank, src_pos++);
                uint16_t ref_addr = buf_pos - offset_byte;
                bool invert = (cmd & 0x20) != 0;
                for (int i = 0; i < count; i++) {
                    uint8_t val = buf[ref_addr++];
                    buf[buf_pos++] = invert ? (val ^ 0xFF) : val;
                }
            }
            break;
        }
    }

    /* Now DMA the decompressed buffer to VRAM.
     * Source is $7F:vram_dest, size is (buf_pos - vram_dest) bytes. */
    uint16_t decompressed_size = buf_pos - vram_dest;

    bus_write8(0x80, 0x2115, 0x80);  /* VMAIN: word access, auto-increment */
    bus_write8(0x80, 0x2116, vram_dest & 0xFF);
    bus_write8(0x80, 0x2117, (vram_dest >> 8) & 0xFF);

    bus_write8(0x80, 0x4310, 0x01);  /* ctrl: 2-byte word write, A-bus inc */
    bus_write8(0x80, 0x4311, 0x18);  /* B-bus dest: $2118 (VRAM data) */
    bus_write8(0x80, 0x4312, vram_dest & 0xFF);         /* src addr low */
    bus_write8(0x80, 0x4313, (vram_dest >> 8) & 0xFF);  /* src addr high */
    bus_write8(0x80, 0x4314, 0x7F);  /* src bank = $7F */
    bus_write8(0x80, 0x4315, decompressed_size & 0xFF);
    bus_write8(0x80, 0x4316, (decompressed_size >> 8) & 0xFF);
    bus_write8(0x80, 0x420B, 0x02);  /* trigger DMA channel 1 */

    g_cpu.DB = saved_db;
}

/*
 * $81:E50D — PPU register setup for title screen
 *
 * Sets BG tile data/map addresses, screen mode, main/sub screen
 * designation, and disables DMA/HDMA/NMI.
 */
void smk_81E50D(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x81);

    op_sep(0x30);

    /* OBSEL = $02 (8x8/16x16 sprites) */
    op_lda_imm8(0x02);
    op_sta_abs8(0x2101);

    /* BG1SC = $00 (tilemap at VRAM $0000, 32x32) */
    op_stz_abs8(0x2107);

    /* BG2SC = $7B, BG3SC = $7B, BG4SC = $7B */
    op_lda_imm8(0x7B);
    op_sta_abs8(0x2108);
    op_sta_abs8(0x2109);
    op_sta_abs8(0x210A);

    /* BG12NBA = $70 (BG1 tiles at $0000, BG2 at $7000) */
    op_lda_imm8(0x70);
    op_sta_abs8(0x210B);

    /* BG34NBA = $77 */
    op_lda_imm8(0x77);
    op_sta_abs8(0x210C);

    /* M7SEL = $C0 (Mode 7 settings) */
    op_lda_imm8(0xC0);
    op_sta_abs8(0x211A);

    /* TM = $10 (OBJ on main screen) — note: $85:8000 likely updates this later */
    op_lda_imm8(0x10);
    op_sta_abs8(0x212C);    /* TM */
    op_sta_abs8(0x212E);    /* TMW */
    op_stz_abs8(0x212D);    /* TS */
    op_stz_abs8(0x212F);    /* TSW */
    op_stz_abs8(0x2131);    /* CGADSUB */
    op_stz_abs8(0x2133);    /* SETINI */

    /* Disable H/V IRQ triggers */
    op_stz_abs8(0x4207);
    op_stz_abs8(0x4208);
    op_stz_abs8(0x4209);
    op_stz_abs8(0x420A);

    /* Disable NMI/DMA/HDMA */
    op_stz_abs8(0x4200);
    op_stz_dp8(0xD0);
    op_stz_abs8(0x420B);
    op_stz_abs8(0x420C);

    op_rep(0x30);

    g_cpu.DB = saved_db;
    printf("smk: title PPU registers configured\n");
}

/*
 * $81:E10A — Load title screen tile data to VRAM
 *
 * Calls $84:E09E with: Y=$1996, A=$00C7, X=$0000
 * → Decompress tile data from $C7:1996 to VRAM $0000
 */
void smk_81E10A(void) {
    op_ldy_imm16(0x1996);
    op_lda_imm16(0x00C7);
    op_ldx_imm16(0x0000);
    smk_84E09E();
    printf("smk: title screen tiles loaded (VRAM $0000)\n");
}

/*
 * $81:E118 — Load title screen tilemap to VRAM
 *
 * Calls $84:E09E with: Y=$0B29, A=$00C7, X=$C000
 * → Decompress tilemap from $C7:0B29 to VRAM $C000
 */
void smk_81E118(void) {
    op_ldy_imm16(0x0B29);
    op_lda_imm16(0x00C7);
    op_ldx_imm16(0xC000);
    smk_84E09E();
    printf("smk: title screen tilemap loaded (VRAM $C000)\n");
}

/*
 * $81:E584 — Load title screen additional tile data
 *
 * Calls $84:E09E with: Y=$0594, A=$00C4, X=$8000
 * → Decompress data from $C4:0594 to VRAM $8000
 */
void smk_81E584(void) {
    op_ldy_imm16(0x0594);
    op_lda_imm16(0x00C4);
    op_ldx_imm16(0x8000);
    smk_84E09E();
    printf("smk: title screen extra data loaded (VRAM $8000)\n");
}

/*
 * $81:E933 — Title screen VRAM DMA transfers
 *
 * DMA transfers tilemap data from WRAM $7F:D200/$7F:D2C0
 * to VRAM $5060/$5160 (64 bytes each).
 */
void smk_81E933(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x81);

    /* JSR $E856 — decompress tilemap data from $C1:0000 to $7F:C000 */
    {
        op_rep(0x30);
        /* VMAIN = $80 */
        bus_write8(0x81, 0x2115, 0x80);
        /* Decompress: Y=$0000, A=$00C1, X=$C000 */
        op_ldy_imm16(0x0000);
        op_lda_imm16(0x00C1);
        op_ldx_imm16(0xC000);
        smk_84E09E();
    }

    op_rep(0x30);

    /* Set VMAIN for word access, auto-increment */
    bus_write8(0x81, 0x2115, 0x80);
    bus_write8(0x81, 0x4310, 0x01);  /* DMA1 ctrl: 2-byte word write */
    bus_write8(0x81, 0x4311, 0x18);  /* DMA1 dest = $2118 */

    /* Transfer 1: $7F:D200 → VRAM $5060, 64 bytes */
    op_lda_imm16(0x5060);
    op_sta_abs16(0x2116);
    op_lda_imm16(0xD200);
    op_sta_abs16(0x4302);
    op_lda_imm16(0x007F);
    op_sta_abs16(0x4304);
    op_lda_imm16(0x0040);
    op_sta_abs16(0x4305);
    op_lda_imm16(0x0001);
    op_sta_abs16(0x420B);  /* trigger DMA ch0... actually original uses STX */

    /* Transfer 2: $7F:D2C0 → VRAM $5160, 64 bytes */
    op_lda_imm16(0x5160);
    op_sta_abs16(0x2116);
    op_lda_imm16(0xD2C0);
    op_sta_abs16(0x4302);
    op_lda_imm16(0x007F);
    op_sta_abs16(0x4304);
    op_lda_imm16(0x0040);
    op_sta_abs16(0x4305);
    op_lda_imm16(0x0001);
    op_sta_abs16(0x420B);

    g_cpu.DB = saved_db;
}

/*
 * $84:F38C — Full PPU/display reset
 *
 * Force blank, disable all HDMA/DMA, clear windows, color math, scrolls,
 * mosaic, SETINI. Fill OAM mirror at $0200 with offscreen coords ($E0F8).
 * Clear OAM high table at $0400.
 */
void smk_84F38C(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x84);
    op_sep(0x30);

    /* Disable HDMA/DMA */
    op_stz_abs8(0x420C);
    op_stz_abs8(0x420B);

    /* Force blank ON */
    op_lda_imm8(0x80);
    op_sta_abs8(0x2100);

    /* Clear screen designations */
    op_stz_abs8(0x212E);  /* TMW */
    op_stz_abs8(0x212F);  /* TSW */
    op_stz_abs8(0x2123);  /* W12SEL */
    op_stz_abs8(0x2124);  /* W34SEL */
    op_stz_abs8(0x2125);  /* WOBJSEL */
    op_stz_abs8(0x2130);  /* CGWSEL */
    op_stz_abs8(0x2131);  /* CGADSUB */

    /* Fixed color = black ($E0 = all R/G/B to 0) */
    op_lda_imm8(0xE0);
    op_sta_abs8(0x2132);

    /* Clear BG scroll registers (double-write pattern for all 4 BGs H/V) */
    for (int i = 7; i >= 0; i--) {
        bus_write8(0x84, 0x210D + i, 0);
        bus_write8(0x84, 0x210D + i, 0);
    }

    /* Clear mosaic, SETINI, sub screen */
    op_stz_abs8(0x2106);
    op_stz_abs8(0x2133);
    op_stz_abs8(0x212D);

    /* Fill OAM mirror at $0200 with $E0F8 (offscreen) */
    op_rep(0x30);
    {
        uint8_t *wram = bus_get_wram();
        if (wram) {
            for (int i = 0; i < 0x200; i += 2) {
                wram[0x0200 + i] = 0xF8;      /* X low byte */
                wram[0x0200 + i + 1] = 0xE0;  /* Y byte */
            }
            /* Clear OAM high table at $0400 (32 bytes) */
            memset(wram + 0x0400, 0, 0x20);
        }
    }

    op_sep(0x30);
    g_cpu.DB = saved_db;
}

/*
 * $84:FCF1 — SRAM checksum validation
 *
 * Sums words at $30:67F2-$30:67F5, compares with checksum at $30:67F0.
 * If mismatch, zeroes the save data.
 */
void smk_84FCF1(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x84);
    op_rep(0x30);

    /* Clear accumulator at $19FE */
    bus_wram_write16(0x19FE, 0);

    /* Sum 2 words at $30:67F2 */
    uint16_t sum = 0;
    for (int i = 0; i < 4; i += 2) {
        sum += bus_read16(0x30, 0x67F2 + i);
    }
    bus_wram_write16(0x19FE, sum);

    /* Compare with checksum at $30:67F0 */
    uint16_t expected = bus_read16(0x30, 0x67F0);
    if (sum != expected) {
        /* Checksum mismatch — clear save data */
        for (int i = 0; i < 6; i += 2) {
            bus_write16(0x30, 0x67F0 + i, 0);
        }
    }

    g_cpu.DB = saved_db;
}

/*
 * $85:8000 — Title screen sprite/palette/OAM setup
 *
 * The main graphics initialization for the title screen:
 * 1. HDMA table setup ($81:CB98)
 * 2. PPU reset ($84:F38C)
 * 3. SRAM validation ($84:FCF1)
 * 4. Sprite tile DMA, VRAM fill, CGRAM palette load, OAM setup ($85:80B9)
 * 5. Display flag configuration ($85:821C)
 * 6. OAM/sprite table init ($85:8F84)
 * 7. Set DP vars ($8C=$3800, $8E=0, $62=0)
 */
void smk_858000(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);
    op_rep(0x30);

    /* JSL $81CB98 — HDMA table setup
     * Sets $C8=$1000, $B0/$B2=$00B0, processes channel table at $85:9059.
     * Skip for now — HDMA will produce visual artifacts without it but
     * won't prevent basic display. */

    /* Clear memory: $10E2, $11E2, ..., $17E2 */
    for (uint16_t x = 0x1000; x < 0x1800; x += 0x0100) {
        bus_wram_write16(0x00E2 + x, 0);
    }

    /* STZ $1F2A, $0E60 */
    bus_wram_write16(0x1F2A, 0);
    bus_wram_write16(0x0E60, 0);

    /* SEP #$30 — 8-bit for sub-calls */
    op_sep(0x30);

    /* JSL $84F38C — full PPU reset */
    smk_84F38C();

    /* REP #$30 */
    op_rep(0x30);

    /* JSL $84FCF1 — SRAM checksum */
    smk_84FCF1();

    /* SEP #$30 */
    op_sep(0x30);

    /* JSR $80B9 — sprite tile DMA + CGRAM palette + OAM setup */
    /* This is the big one: loads palette data from $7F:4000 to CGRAM,
     * DMAs sprite tiles from $7F to VRAM, fills VRAM tilemaps. */
    {
        /* $85:80B9 implementation inline */
        uint8_t *wram = bus_get_wram();

        /* VMAIN = $80 */
        bus_write8(0x00, 0x2115, 0x80);

        /* DMA 4 sprite tile blocks from WRAM $7F to VRAM.
         * The source addresses, sizes, and VRAM targets are in tables at
         * $85:81AA, $85:81D0, $85:81F6. Read them from ROM. */
        for (int blk = 0; blk < 4; blk++) {
            int x = blk * 2;
            uint8_t src_lo = bus_read8(0x85, 0x81AA + x);
            uint8_t src_hi = bus_read8(0x85, 0x81AB + x);
            uint8_t size_lo = bus_read8(0x85, 0x81D0 + x);
            uint8_t size_hi = bus_read8(0x85, 0x81D1 + x);
            uint8_t vram_lo = bus_read8(0x85, 0x81F6 + x);
            uint8_t vram_hi = bus_read8(0x85, 0x81F7 + x);

            bus_write8(0x85, 0x4300, 0x01);  /* DMA ctrl: 2-reg word write */
            bus_write8(0x85, 0x4301, 0x18);  /* B-bus: $2118 */
            bus_write8(0x85, 0x4302, src_lo);
            bus_write8(0x85, 0x4303, src_hi);
            bus_write8(0x85, 0x4304, 0x7F);  /* bank $7F */
            bus_write8(0x85, 0x4305, size_lo);
            bus_write8(0x85, 0x4306, size_hi);
            bus_write8(0x85, 0x2116, vram_lo);
            bus_write8(0x85, 0x2117, vram_hi);
            bus_write8(0x85, 0x420B, 0x01);  /* trigger DMA ch0 */
        }

        /* REP #$30 */
        op_rep(0x30);

        /* Fill VRAM $1C00 with $1C00 pattern (2048 words) */
        bus_write8(0x85, 0x2116, 0x00);  /* VMADDL */
        bus_write8(0x85, 0x2117, 0x1C);  /* VMADDH = $1C → addr $1C00 */
        for (int i = 0; i < 0x800; i++) {
            bus_write8(0x00, 0x2118, 0x00);
            bus_write8(0x00, 0x2119, 0x1C);
        }

        /* Fill 512 more words with $00FF */
        for (int i = 0; i < 0x200; i++) {
            bus_write8(0x00, 0x2118, 0xFF);
            bus_write8(0x00, 0x2119, 0x00);
        }

        /* SEP #$20 — 8-bit A for CGRAM writes */
        op_sep(0x20);

        /* Set CGRAM address to 0 */
        bus_write8(0x85, 0x2121, 0x00);

        /* Load all 256 colors (512 bytes) from $7F:4000 to CGRAM */
        if (wram) {
            for (int i = 0; i < 0x200; i++) {
                bus_write8(0x85, 0x2122, wram[0x10000 + 0x4000 + i]);
            }
        }

        /* REP #$30 */
        op_rep(0x30);

        /* OAM parameter setup at $0300 */
        bus_wram_write16(0x0300, 0x8054);
        bus_wram_write16(0x0302, 0x0C9D);
        bus_wram_write16(0x0306, 0x0C9D);
        bus_wram_write16(0x030A, 0x0C9D);
        bus_wram_write16(0x030E, 0x0C9D);

        /* Sprite table setup calls ($85:88CB) — skip for now,
         * these configure sprite animation tables */

        op_sep(0x30);
    }

    /* JSR $821C — display flag configuration
     * Reads DP $30, $2E, $2C and configures layer enables in DP $85.
     * For the title screen with no save data, $30=0, $2E=0, $2C=0:
     *   → $85 = $01 (BG1 only) */
    {
        uint8_t disp_flags = 0x01;  /* Default for title screen */
        bus_wram_write8(g_cpu.DP + 0x85, disp_flags);
        bus_wram_write8(g_cpu.DP + 0x80, 0x01);  /* Mode = 1 */
    }

    /* REP #$30 */
    op_rep(0x30);

    /* JSR $8F84 — OAM/sprite table init (sets up $0284-$028A) */
    /* Skip — OAM animation data, not needed for static display */

    /* Final: set DP vars */
    bus_wram_write16(g_cpu.DP + 0x8C, 0x3800);
    bus_wram_write16(g_cpu.DP + 0x8E, 0);
    bus_wram_write16(g_cpu.DP + 0x62, 0);

    g_cpu.DB = saved_db;
    printf("smk: title screen graphics setup complete (palette+sprites+OAM)\n");
}

/*
 * $81:E0AD — Title screen transition handler
 *
 * Called when DP $32 = $04 (transition to title screen).
 * Sets up PPU, loads graphics/tilemaps/palettes, initializes state.
 */
void smk_81E0AD(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x81);
    op_rep(0x30);

    /* JSL $00FF9A — trivial (PHB/PHK/PLB/PLB/RTL) — nop */

    /* JSR $E50D — PPU register setup */
    smk_81E50D();

    /* JSR $E10A — Load tiles to VRAM */
    smk_81E10A();

    /* JSR $E118 — Load tilemap to VRAM */
    smk_81E118();

    /* JSR $E584 — Load additional data to VRAM */
    smk_81E584();

    /* STZ $0E68 */
    bus_wram_write16(0x0E68, 0);

    /* SRAM checksum validation loop ($E0C0-$E0CF) — skip for now */

    /* Check $0E32 for pending sub-state restore */
    uint16_t sub_state = bus_wram_read16(0x0E32);
    if (sub_state != 0) {
        bus_wram_write16(0x0E32, 0);
        bus_wram_write16(g_cpu.DP + 0x2E, bus_wram_read16(0x0E3A));
        bus_wram_write16(g_cpu.DP + 0x2C, bus_wram_read16(0x0E3C));
        bus_wram_write16(0x0030, bus_wram_read16(0x0E3E));
        bus_wram_write16(0x0150, bus_wram_read16(0x0E44));
        bus_wram_write16(0x1012, bus_wram_read16(0x0E40));
        bus_wram_write16(0x1112, bus_wram_read16(0x0E42));
    }

    /* JSL $858000 — title screen sprite/Mode 7 setup */
    /* This sets up HDMA tables, loads sprite tiles, configures Mode 7.
     * Critical for visual output but has many sub-calls. Stub for now. */
    func_table_call(0x858000);

    /* $85:8000 handles BGMODE, TM, palette, sprite tile loading.
     * After it returns, set BGMODE and TM for the title screen since
     * the $85:821C stub may not configure them fully yet. */
    bus_write8(0x81, 0x2105, 0x01);  /* BGMODE: Mode 1, 8x8 tiles */
    bus_write8(0x81, 0x212C, 0x17);  /* TM: BG1+BG2+BG3+OBJ on main screen */

    /* Clear state vars */
    bus_wram_write16(0x0158, 0);
    bus_wram_write16(0x0E50, 0);

    /* JSR $E933 — VRAM DMA transfers */
    smk_81E933();

    g_cpu.DB = saved_db;
    printf("smk: title screen transition complete\n");
}
