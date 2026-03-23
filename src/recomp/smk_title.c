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

            /* Count: 1 data byte + cmd bits 0-1 as high 2 bits (10-bit total).
             * Original 65816 does overlapping reads to merge cmd into count:
             *   STA $12 = word@[src] → $12=lo, $13=hi
             *   STA $13 = word@[src-1] → $13=cmd, $14=lo
             *   LDA $12 → lo | (cmd << 8), AND #$03FF */
            uint8_t lo = bus_read8(data_bank, src_pos);
            src_pos += 1;
            count = (lo | ((uint16_t)(cmd & 0x03) << 8)) + 1;
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
            /* Back-reference: read 16-bit offset, add vram_dest (DP $00) */
            {
                uint16_t ref_lo = bus_read8(data_bank, src_pos);
                uint16_t ref_hi = bus_read8(data_bank, src_pos + 1);
                src_pos += 2;
                uint16_t ref_addr = (ref_lo | (ref_hi << 8)) + vram_dest;
                for (int i = 0; i < count; i++) {
                    buf[buf_pos] = buf[ref_addr];
                    buf_pos++;
                    ref_addr++;
                }
            }
            break;

        case 0xA0:
            /* Back-reference with XOR $FF: read 16-bit offset, add vram_dest */
            {
                uint16_t ref_lo = bus_read8(data_bank, src_pos);
                uint16_t ref_hi = bus_read8(data_bank, src_pos + 1);
                src_pos += 2;
                uint16_t ref_addr = (ref_lo | (ref_hi << 8)) + vram_dest;
                for (int i = 0; i < count; i++) {
                    buf[buf_pos] = buf[ref_addr] ^ 0xFF;
                    buf_pos++;
                    ref_addr++;
                }
            }
            break;

        case 0xC0:
        case 0xE0:
            /* Byte-offset back-reference: ref = buf_pos - offset_byte.
             * Mode $E0 (only from E0+ extended cmds) inverts the copy. */
            {
                uint8_t offset_byte = bus_read8(data_bank, src_pos++);
                uint16_t ref_addr = buf_pos - offset_byte;
                bool invert = (mode == 0xE0);
                for (int i = 0; i < count; i++) {
                    uint8_t val = buf[ref_addr++];
                    buf[buf_pos++] = invert ? (val ^ 0xFF) : val;
                }
            }
            break;
        }
    }

    /* Decompressor only writes to $7F WRAM buffer — DMA to VRAM is done
     * separately by the caller (e.g. $85:8171 for tile data). */
    uint16_t decompressed_size = buf_pos - vram_dest;
    printf("smk: decompress $%02X:%04X → $7F:%04X, size=%04X, end=$7F:%04X\n",
           data_bank, data_ptr, vram_dest, decompressed_size, buf_pos);

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
    {
        uint8_t *wram = bus_get_wram();
        if (wram) {
            int nz = 0;
            for (int i = 0; i < 0x200; i++) if (wram[0x14000+i]) nz++;
            printf("smk: tilemap loaded, $7F:4000 nonzero=%d\n", nz);
        }
    }
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
    {
        uint8_t *wram = bus_get_wram();
        if (wram) {
            int nz = 0;
            for (int i = 0; i < 0x200; i++) if (wram[0x14000+i]) nz++;
            printf("smk: extra loaded, $7F:4000 nonzero=%d\n", nz);
        }
    }
}

/*
 * $81:E576 — Sprite tile decompression + 2bpp→4bpp interleave
 *
 * Called during init ($81:E000) to prepare sprite tile data:
 * 1. $81:E592 — Decompress 2bpp sprite tile source from $C7:0000 → $7F:4400
 * 2. $81:E5A0 — Interleave 2bpp → 4bpp: read from $7F:4400, write to $7F:A000
 *              Each 2bpp tile row (2 bytes, planes 0-1) gets padded with 2 zero
 *              bytes (planes 2-3) to make a 4bpp tile row (4 bytes).
 *              8KB source → 16KB output at $7F:A000-$7F:DFFF.
 * 3. $81:E584 — Decompress additional data from $C4:0594 → $7F:8000
 */
void smk_81E576(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x81);
    op_rep(0x30);

    /* JSR $E592 — Decompress sprite tiles from $C7:0000 → $7F:4400 */
    op_ldy_imm16(0x0000);
    op_lda_imm16(0x00C7);
    op_ldx_imm16(0x4400);
    smk_84E09E();

    /* JSR $E5A0 — 2bpp→4bpp interleave: $7F:4400 → $7F:A000 */
    {
        uint8_t *wram = bus_get_wram();
        if (wram) {
            uint8_t *src = wram + 0x10000;  /* $7F bank base */
            uint16_t src_off = 0x4400;
            uint16_t dst_off = 0xA000;

            while (src_off < 0x4400 + 0x2000) {
                /* Copy 8 words (16 bytes) from source — planes 0-1 of 8 rows */
                for (int i = 0; i < 16; i++) {
                    src[dst_off++] = src[src_off++];
                }
                /* Pad 8 words (16 bytes) of zeros — planes 2-3 */
                for (int i = 0; i < 16; i++) {
                    src[dst_off++] = 0;
                }
            }
            printf("smk: sprite tiles interleaved: $7F:4400 → $7F:A000 (%d bytes)\n",
                   dst_off - 0xA000);
        }
    }

    /* JSR $E584 — Decompress additional data */
    smk_81E584();

    g_cpu.DB = saved_db;
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
 * $84:F421 — Viewport and HDMA parameter setup
 *
 * Sets display configuration variables and HDMA window parameters.
 * Called during mode select/race screen transitions.
 */
void smk_84F421(void) {
    op_rep(0x30);

    /* Display/window config in DP */
    bus_wram_write16(g_cpu.DP + 0x84, 0x0802);
    bus_wram_write16(g_cpu.DP + 0x86, 0x2713);
    bus_wram_write16(g_cpu.DP + 0x88, 0x2718);

    /* HDMA-related window sizes */
    bus_wram_write16(0x0180, 0x0008);
    bus_wram_write16(0x0182, 0x0008);
    bus_wram_write16(0x0184, 0x0002);
    bus_wram_write16(0x0186, 0x0002);

    /* Clear flags */
    bus_wram_write16(0x0E66, 0);

    /* Clear bit 14 of player config words */
    uint16_t v1 = bus_wram_read16(0x10E2);
    bus_wram_write16(0x10E2, v1 & 0xBFFF);
    uint16_t v2 = bus_wram_read16(0x11E2);
    bus_wram_write16(0x11E2, v2 & 0xBFFF);
}

/*
 * $84:F45A — PPU register setup for Mode 0 screens
 *
 * Configures PPU for the mode select / race setup screens.
 * Mode 0, all layers enabled, BG tilemap addresses for 4 layers.
 */
void smk_84F45A(void) {
    op_sep(0x30);

    bus_write8(0x84, 0x2105, 0x00);  /* BGMODE: Mode 0 */
    bus_write8(0x84, 0x212C, 0x1F);  /* TM: BG1+BG2+BG3+BG4+OBJ */
    bus_write8(0x84, 0x2107, 0x24);  /* BG1SC: tilemap at VRAM $2400, 32x32 */
    bus_write8(0x84, 0x2108, 0x28);  /* BG2SC: tilemap at VRAM $2800, 32x32 */
    bus_write8(0x84, 0x2109, 0x2C);  /* BG3SC: tilemap at VRAM $2C00, 32x32 */
    bus_write8(0x84, 0x210A, 0x38);  /* BG4SC: tilemap at VRAM $3800, 32x32 */
    bus_write8(0x84, 0x210B, 0x33);  /* BG12NBA: BG1 tiles $3000, BG2 tiles $3000 */
    bus_write8(0x84, 0x210C, 0x33);  /* BG34NBA: BG3 tiles $3000, BG4 tiles $3000 */
    bus_write8(0x84, 0x2101, 0x02);  /* OBSEL: 8x8/16x16 sprites */
    bus_write8(0x84, 0x2115, 0x80);  /* VMAIN: word access, inc on high */
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

    /* JSL $81CB98 — HDMA table setup + sprite slot initialization
     * The real routine builds a linked list for the OAM builder and
     * initializes 8 sprite slots from the config table at $85:9059.
     * We skip the linked list/HDMA but DO initialize the slot data. */
    {
        /* Read 8 entries from $85:9059 (8 bytes each: slot, X, Y, tile) */
        uint16_t tbl = 0x9059;
        for (int i = 0; i < 8; i++) {
            uint16_t slot_num = bus_read16(0x85, tbl);
            if (slot_num == 0) break;
            uint16_t x_pos = bus_read16(0x85, tbl + 2);
            uint16_t y_pos = bus_read16(0x85, tbl + 4);
            uint16_t tile  = bus_read16(0x85, tbl + 6);
            tbl += 8;

            /* Initialize slot fields (mimics $81:CB98 + $81:CBFF) */
            bus_wram_write16(slot_num + 0x18, x_pos);
            bus_wram_write16(slot_num + 0x1C, y_pos);
            bus_wram_write16(slot_num + 0x2A, tile);
            bus_wram_write16(slot_num + 0xBA, 0x0140);
            bus_wram_write16(slot_num + 0x30, 0x0120);
            bus_wram_write16(slot_num + 0x44, y_pos); /* target Y = initial Y */
            bus_wram_write16(slot_num + 0x22, 0);
            bus_wram_write16(slot_num + 0x40, 0);
            bus_wram_write16(slot_num + 0x42, 0);
            bus_wram_write16(slot_num + 0x86, 0);
        }
        bus_wram_write16(g_cpu.DP + 0xC8, 0x1000);
        bus_wram_write16(g_cpu.DP + 0xB0, 0x00B0);
        bus_wram_write16(g_cpu.DP + 0xB2, 0x00B0);
    }

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

        /* Load 256 colors (512 bytes) from $7F:4000 to CGRAM.
         * This is the palette data decompressed by $81:E10A. */
        if (wram) {
            int nz = 0;
            for (int i = 0; i < 0x200; i++) {
                if (wram[0x10000 + 0x4000 + i]) nz++;
                bus_write8(0x85, 0x2122, wram[0x10000 + 0x4000 + i]);
            }
            printf("smk: loaded CGRAM from $7F:4000 (%d/512 bytes nonzero)\n", nz);
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
     * For the title screen with no save data ($30=0, $2E=0, $2C=0):
     *   $85 = $01 (BG1 enabled flag)
     *   $80 is set to 1, JSR $8865 runs (sprite config init), then $80 = 0
     *   PPU registers set at $85:8277 */
    {
        /* DP $85 = display flags. $30=0 → skip first branch.
         * $2E=0 → ORA #$01 → $85=$01. $2C=0 → skip. */
        bus_wram_write8(g_cpu.DP + 0x85, 0x01);

        /* $85:826E: $2E < 2 → run sprite init.
         * Sets $80=1, calls $8865 (sprite table setup), then $80=0. */
        /* $85:8865 clears sprite config buffers and initializes sprite slots.
         * For now, stub it — just clear the buffers. */
        {
            uint8_t *wram = bus_get_wram();
            if (wram) {
                /* Fill $0310-$03FF with $E0F8 (offscreen) */
                for (int i = 0; i < 0xF0; i += 2) {
                    wram[0x0310 + i] = 0xF8;
                    wram[0x0310 + i + 1] = 0xE0;
                }
                /* Fill $0300-$030F with $E0F8 */
                for (int i = 0; i < 0x10; i += 2) {
                    wram[0x0300 + i] = 0xF8;
                    wram[0x0300 + i + 1] = 0xE0;
                }
            }
        }
        bus_wram_write8(g_cpu.DP + 0x80, 0x00);  /* STZ $80 — mode 0 (OAM build) */
    }

    /* REP #$30 */
    op_rep(0x30);

    /* JSR $8F84 — OAM/sprite table init (sets up $0284-$028A) */
    /* Skip for now — OAM animation data tables */

    /* Final: set DP vars */
    bus_wram_write16(g_cpu.DP + 0x8C, 0x3800);
    bus_wram_write16(g_cpu.DP + 0x8E, 0);
    bus_wram_write16(g_cpu.DP + 0x62, 0);

    g_cpu.DB = saved_db;
    printf("smk: title screen graphics setup complete\n");
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

    /* PPU registers from $85:8277 (applied by $85:821C display flags path) */
    bus_write8(0x81, 0x2105, 0x01);  /* BGMODE: Mode 1 */
    bus_write8(0x81, 0x212C, 0x17);  /* TM: BG1+BG2+BG3+OBJ */
    bus_write8(0x81, 0x2123, 0x22);  /* W12SEL: window 1 enable for BG2+BG3 */
    bus_write8(0x81, 0x212E, 0x03);  /* TMW: window masking on BG1+BG2 */
    bus_write8(0x81, 0x2107, 0x10);  /* BG1SC: tilemap at VRAM $1000, 32x32 */
    bus_write8(0x81, 0x2108, 0x15);  /* BG2SC: tilemap at VRAM $1400, 64x32 */
    bus_write8(0x81, 0x2109, 0x1C);  /* BG3SC: tilemap at VRAM $1C00, 32x32 */
    bus_write8(0x81, 0x210B, 0x00);  /* BG12NBA: BG1+BG2 tiles at $0000 */
    bus_write8(0x81, 0x210C, 0x22);  /* BG34NBA: BG3+BG4 tiles at $2000 */
    bus_write8(0x81, 0x2101, 0x02);  /* OBSEL: 8x8/16x16 sprites */

    /* VMAIN = $80 (word access, increment on high byte write) */
    bus_write8(0x81, 0x2115, 0x80);

    /* $85:82AE — HDMA channel 1 configuration
     * Indirect mode, 2-register write to $2126/$2127 (window 1 left/right).
     * Table at WRAM $0180, indirect data bank = $85. */
    bus_write8(0x81, 0x4310, 0x41);  /* $4310 = $41: indirect HDMA, 2-reg write */
    bus_write8(0x81, 0x4311, 0x26);  /* $4311 = $26: B-bus target $2126/$2127 */
    bus_write8(0x81, 0x4312, 0x80);  /* $4312 = $80: table addr low ($0180) */
    bus_write8(0x81, 0x4313, 0x01);  /* $4313 = $01: table addr high */
    bus_write8(0x81, 0x4314, 0x00);  /* $4314 = $00: table bank ($00 = WRAM) */
    bus_write8(0x81, 0x4315, 0x00);  /* $4315 = $00 */
    bus_write8(0x81, 0x4316, 0x00);  /* $4316 = $00 */
    bus_write8(0x81, 0x4317, 0x85);  /* $4317 = $85: indirect data bank */

    /* Copy 13-byte HDMA table from ROM $85:82E2 to WRAM $0180 */
    {
        static const uint8_t hdma_table[13] = {
            0x2C, 0x09, 0x83, 0x60, 0x09, 0x83,
            0x28, 0x09, 0x83, 0x70, 0x09, 0x83,
            0x00
        };
        uint8_t *wram = bus_get_wram();
        if (wram) {
            for (int i = 0; i < 13; i++) {
                wram[0x0180 + i] = hdma_table[i];
            }
        }
    }

    /* Clear state vars */
    bus_wram_write16(0x0158, 0);
    bus_wram_write16(0x0E50, 0);

    /* JSR $E933 — VRAM DMA transfers */
    smk_81E933();

    g_cpu.DB = saved_db;
    printf("smk: title screen transition complete\n");
}

/*
 * Title screen animation — slot-based sprite state machine
 *
 * 8 sprite slots ($1000-$1700), each with:
 *   $18,x = initial X position
 *   $1C,x = Y position (current, interpolated toward $44,x)
 *   $22,x = X position / movement speed
 *   $2A,x = tile/attribute word
 *   $40,x = frame counter (incremented each frame)
 *   $42,x = phase counter (incremented at milestone frames)
 *   $44,x = Y target position
 *   $86,x = flags
 *   $92,x = config
 *
 * Original at $85:8B7A, called from mode 0 path ($85:8061).
 */

/* Helper: read/write slot fields using WRAM access */
static uint16_t slot_read(uint16_t slot_base, uint16_t field) {
    return bus_wram_read16(slot_base + field);
}
static void slot_write(uint16_t slot_base, uint16_t field, uint16_t val) {
    bus_wram_write16(slot_base + field, val);
}

/*
 * $85:8EE9 — Y position interpolation engine
 *
 * Processes one slot per call (cycles through 8 slots via DP $75).
 * Moves $1C,x toward $44,x by +/-1 each time.
 */
static void smk_85_8EE9(void) {
    /* Slot base table (same as $85:8FE9) */
    static const uint16_t slot_bases[8] = {
        0x1000, 0x1100, 0x1200, 0x1300,
        0x1400, 0x1500, 0x1600, 0x1700
    };

    uint16_t idx = bus_wram_read16(g_cpu.DP + 0x75);
    uint16_t slot = slot_bases[idx / 2];

    uint16_t current = slot_read(slot, 0x1C);
    uint16_t target = slot_read(slot, 0x44);

    if (current == target) {
        current++;
    } else {
        current--;
    }
    slot_write(slot, 0x1C, current);

    idx += 2;
    if (idx >= 0x10) idx = 0;
    bus_wram_write16(g_cpu.DP + 0x75, idx);
}

/*
 * $85:8FB8 — Initialize slot from data table
 *
 * Y = table index (0, 2, 4, ..., 14)
 * X = slot base
 */
static void smk_85_8FB8(uint16_t slot, uint16_t table_idx) {
    /* Pointer table at $85:8FF9 */
    static const uint16_t ptrs[8] = {
        0x9009, 0x9013, 0x901D, 0x9027,
        0x9031, 0x903B, 0x9045, 0x904F
    };
    uint16_t ptr = ptrs[table_idx / 2];

    /* Read 5 words of init data from ROM at $85:ptr */
    uint16_t x_pos    = bus_read16(0x85, ptr + 0);
    uint16_t y_pos    = bus_read16(0x85, ptr + 2);
    uint16_t tile     = bus_read16(0x85, ptr + 4);
    uint16_t cfg      = bus_read16(0x85, ptr + 6);
    uint16_t target_y = bus_read16(0x85, ptr + 8);

    slot_write(slot, 0x22, 0);   /* STZ $22,x */
    slot_write(slot, 0x18, x_pos);
    slot_write(slot, 0x1C, y_pos);
    slot_write(slot, 0x2A, tile);
    slot_write(slot, 0x92, cfg);
    slot_write(slot, 0x44, target_y);
}

/*
 * $85:8F0B — Advance sprite tile
 *
 * $2A,x += $0800
 */
static void smk_85_8F0B(uint16_t slot) {
    uint16_t v = slot_read(slot, 0x2A);
    slot_write(slot, 0x2A, v + 0x0800);
}

/*
 * $85:8F2E — Sprite animation update
 *
 * Updates OAM high table, palette cycling, frame counter for sprite 2.
 */
static void smk_85_8F2E(void) {
    bus_wram_write16(0x0408, 0xAAAA);
    uint16_t v = bus_wram_read16(0x0284);
    bus_wram_write16(0x0284, v + 3);

    uint16_t frame = bus_wram_read16(g_cpu.DP + 0x88);
    frame++;
    if (frame >= 5) {
        frame = 0;
        uint16_t oam = bus_wram_read16(0x0286);
        if (oam == 0x3F02)
            bus_wram_write16(0x0286, 0x3F04);
        else
            bus_wram_write16(0x0286, 0x3F02);
    }
    bus_wram_write16(g_cpu.DP + 0x88, frame);
}

/*
 * $85:8B7A — Title screen animation state machine
 *
 * Processes all 8 sprite slots: frame counters, phase transitions,
 * and per-phase actions (position, tile, flag updates).
 */
static void smk_85_8B7A(void) {
    /* Check brightness — only run when fade is complete ($48 == 0) */
    uint16_t brightness = bus_wram_read16(0x0048);
    if (brightness != 0) return;

    /* $85:8EE9 — Y interpolation engine */
    smk_85_8EE9();

    /* NOTE: The real game updates X positions via the linked-list
     * callback chain ($81:CB44 → $80:BA36). The exact movement
     * mechanism involves sprite frame data tables. For now, we use
     * $22,x as a direct X position when active. */

    /* === Slot 0 ($1000) === */
    {
        uint16_t s = 0x1000;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x00D2 || fc == 0x01F2 || fc == 0x0452 ||
            fc == 0x046A || fc == 0x053A || fc == 0x0592)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break; /* skip */
        case 1: slot_write(s, 0x22, 0x0120); break;
        case 2:
            smk_85_8FB8(s, 0);
            slot_write(s, 0x86, 0);
            slot_write(s, 0x44, 0x00C0);
            slot_write(s, 0x1C, 0x00C0);
            break;
        case 3: slot_write(s, 0x22, 0x0200); break;
        case 4:
            slot_write(s, 0x2A, 0x3800);
            slot_write(s, 0x22, 0x0300);
            slot_write(s, 0x86, 0x8000);
            break;
        case 5:
            slot_write(s, 0x22, 0);
            smk_85_8F0B(s);
            break;
        }
    }

    /* === Slot 1 ($1100) === */
    {
        uint16_t s = 0x1100;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x0112 || fc == 0x0232 || fc == 0x0492 || fc == 0x0572)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break;
        case 1: slot_write(s, 0x22, 0x0120); break;
        case 2:
            smk_85_8FB8(s, 2);
            break;
        case 3: slot_write(s, 0x22, 0x0120); break;
        case 4:
            slot_write(s, 0x22, 0xFED0);
            smk_85_8F0B(s);
            break;
        }
    }

    /* === Slot 2 ($1200) === */
    {
        uint16_t s = 0x1200;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x01F2 || fc == 0x0217 || fc == 0x0218 ||
            fc == 0x023E || fc == 0x0302 || fc == 0x04C2 || fc == 0x055A)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break;
        case 1: slot_write(s, 0x22, 0x0140); break;
        case 2: bus_wram_write16(0x0284, 0xB028); break;
        case 3: smk_85_8F2E(); break;
        case 4: bus_wram_write16(0x0284, 0xF0E0); break;
        case 5:
            smk_85_8FB8(s, 4);
            break;
        case 6: slot_write(s, 0x22, 0x0120); break;
        case 7:
            slot_write(s, 0x22, 0xFEB0);
            smk_85_8F0B(s);
            break;
        }
    }

    /* === Slot 3 ($1300) === */
    {
        uint16_t s = 0x1300;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x0152 || fc == 0x0272 || fc == 0x04DA ||
            fc == 0x054A || fc == 0x05BA)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break;
        case 1: slot_write(s, 0x22, 0xFEE0); break;
        case 2:
            smk_85_8FB8(s, 6);
            break;
        case 3: slot_write(s, 0x22, 0x0120); break;
        case 4:
            smk_85_8F0B(s);
            slot_write(s, 0x22, 0xFEE0);
            break;
        case 5:
            smk_85_8FB8(s, 6);
            break;
        }
    }

    /* === Slot 4 ($1400) === */
    {
        uint16_t s = 0x1400;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x02FE || fc == 0x0422)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break;
        case 1: slot_write(s, 0x22, 0x0120); break;
        case 2:
            smk_85_8FB8(s, 8);
            break;
        }
    }

    /* === Slot 5 ($1500) === */
    {
        uint16_t s = 0x1500;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x04AA || fc == 0x0568)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break;
        case 1: slot_write(s, 0x22, 0x0120); break;
        case 2:
            slot_write(s, 0x22, 0xFEB0);
            smk_85_8F0B(s);
            break;
        }
    }

    /* === Slot 6 ($1600) === */
    {
        uint16_t s = 0x1600;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x0192 || fc == 0x023C || fc == 0x0245 || fc == 0x02C2)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break;
        case 1: slot_write(s, 0x22, 0x0120); break;
        case 2:
            smk_85_8F0B(s);
            slot_write(s, 0x22, 0xFE90);
            break;
        case 3:
            smk_85_8F0B(s);
            bus_wram_write16(0x0288, 0xF0E0);
            break;
        case 4:
            smk_85_8FB8(s, 12);
            break;
        }
    }

    /* === Slot 7 ($1700) === */
    {
        uint16_t s = 0x1700;
        slot_write(s, 0x40, slot_read(s, 0x40) + 1);
        uint16_t fc = slot_read(s, 0x40);
        if (fc == 0x0362 || fc == 0x03F0 || fc == 0x0472)
            slot_write(s, 0x42, slot_read(s, 0x42) + 1);

        uint16_t phase = slot_read(s, 0x42);
        switch (phase) {
        case 0: break;
        case 1: slot_write(s, 0x22, 0x0160); break;
        case 2:
            smk_85_8F0B(s);
            slot_write(s, 0x22, 0xFDE0);
            break;
        case 3:
            smk_85_8FB8(s, 14);
            break;
        }
    }

    /* === Epilogue === */
    /* Check $8A, call $8F24 if non-zero */
    /* Additional frame-counter checks on slot 0 (for demo mode) — skip for now */
}

/*
 * $80:FE07 — Per-slot position update (velocity → position)
 *
 * Positions are 24-bit: $19:$18:$17 (X), $1D:$1C:$1B (Y)
 * Velocities are 16-bit signed: $22,x (X), $24,x (Y)
 * Each frame: [$17:$18] += $22,x with carry → $19,x (X axis)
 *             [$1B:$1C] += $24,x with carry → $1D,x (Y axis)
 */
static void smk_update_slot_positions(void) {
    static const uint16_t slots[8] = {
        0x1000, 0x1100, 0x1200, 0x1300,
        0x1400, 0x1500, 0x1600, 0x1700
    };

    for (int i = 0; i < 8; i++) {
        uint16_t s = slots[i];
        uint16_t phase = slot_read(s, 0x42);
        if (phase == 0) continue;  /* inactive slot */

        /* X axis: [$17:$18] += $22,x, carry → $19,x */
        int16_t vx = (int16_t)slot_read(s, 0x22);
        if (vx != 0) {
            uint16_t pos_lo = slot_read(s, 0x17);  /* [$17:$18] as 16-bit */
            uint32_t sum = (uint32_t)pos_lo + (uint16_t)vx;
            bus_wram_write16(s + 0x17, (uint16_t)(sum & 0xFFFF));
            /* Carry propagation to $19,x (8-bit) */
            int carry = (sum >> 16) & 1;
            if (vx < 0) carry -= 1;  /* sign extension: subtract 1 for negative velocity */
            uint8_t hi = bus_wram_read8(s + 0x19);
            hi = (uint8_t)(hi + carry);
            bus_wram_write8(s + 0x19, hi);
        }

        /* Y axis: [$1B:$1C] += $24,x, carry → $1D,x */
        int16_t vy = (int16_t)slot_read(s, 0x24);
        if (vy != 0) {
            uint16_t pos_lo = slot_read(s, 0x1B);
            uint32_t sum = (uint32_t)pos_lo + (uint16_t)vy;
            bus_wram_write16(s + 0x1B, (uint16_t)(sum & 0xFFFF));
            int carry = (sum >> 16) & 1;
            if (vy < 0) carry -= 1;
            uint8_t hi = bus_wram_read8(s + 0x1D);
            hi = (uint8_t)(hi + carry);
            bus_wram_write8(s + 0x1D, hi);
        }
    }
}

/*
 * OAM builder + tile DMA staging buffer ($0EA0)
 *
 * Implements the $80:8EC5 → $80:8F22 pipeline:
 * 1. For each active slot, checks if frame data ($30,x) changed from
 *    cached value ($BA,x). If so, builds DMA staging entries at $0EA0
 *    to load tile graphics from ROM to VRAM.
 * 2. Builds OAM entries using the slot's VRAM tile destination to compute
 *    the correct OAM tile index (nt=1 + offset from $5000).
 *
 * Key ROM tables (bank $80):
 *   $846E[y] — VRAM word destination per slot
 *   $9080[y] — DMA config (lo=bank, hi=size_lo)
 *   $9090[frame_ptr] — frame data (lo=$91FA idx, hi has cnt + flags)
 *   $91FA[idx] — tile source address within bank
 *
 * Screen coords (from $80:CF0E):
 *   screen_x = [$18,x | ($19,x << 8)] + $FF00
 *   screen_y = [$1C,x | ($1D,x << 8)] + $FF00
 */

/* VRAM destination per slot (from $80:846E) */
static const uint16_t vram_dest_table[8] = {
    0x5800, 0x5840, 0x5880, 0x58C0,
    0x5C00, 0x5C40, 0x5C80, 0x5CC0
};
/* DMA config per slot: lo=ROM bank (from $80:9080), hi=DMA size_lo.
 * The ROM table has size_lo=$00 which means 64K on SNES — way too much.
 * Each staging entry transfers one tile row: 16 tiles × 32 bytes = $0200.
 * We encode this as bank in low byte, $02 size_lo in high byte → size $0200. */
/* DMA config per slot from $80:9080: lo=ROM bank, hi=$00 (overwritten by frame flags) */
static const uint16_t dma_config_table[8] = {
    0x00C0, 0x0084, 0x00C1, 0x00C2,
    0x00C3, 0x00C5, 0x00C6, 0x00C4
};

/*
 * Build DMA staging buffer entries for a slot.
 * Mirrors $80:8EC5 → $80:8F22 logic.
 */
static void smk_build_dma_staging(int slot_idx) {
    uint16_t s = 0x1000 + slot_idx * 0x100;

    /* Check if frame data changed: LDA $30,x / CMP $BA,x / BEQ skip */
    uint16_t frame_ptr = slot_read(s, 0x30);
    uint16_t cached = slot_read(s, 0xBA);
    if (frame_ptr == cached) return;  /* no change */

    /* Cache the new value: STA $BA,x */
    slot_write(s, 0xBA, frame_ptr);

    /* Read frame data from ROM: $9090[frame_ptr] */
    uint16_t frame_data = bus_read16(0x80, 0x9090 + (frame_ptr & 0x7FFF));

    uint8_t tile_idx = (uint8_t)(frame_data & 0xFF);
    uint8_t hi_byte = (uint8_t)(frame_data >> 8);
    int cnt = hi_byte & 0x0F;  /* loop count */

    if (cnt == 0) return;  /* no DMA needed */

    /* Look up tile source address from $91FA */
    uint16_t src_addr = bus_read16(0x80, 0x91FA + tile_idx);

    /* VRAM dest and DMA config for this slot */
    uint16_t vram_base = vram_dest_table[slot_idx];
    uint16_t config = dma_config_table[slot_idx];

    /* Original $8F48: STA $17 overwrites config high byte with (hi_byte & $F0).
     * This becomes the DMA size low byte: e.g. $21→$20 = 32 bytes = 1 tile. */
    uint8_t dma_size = hi_byte & 0xF0;

    /* Note: first activation (cached=$0140 sentinel) only transfers cnt=1
     * row (32 bytes). The original game pre-loads base tiles via CB98/CBE4.
     * For now, rely on per-frame updates to fill tiles over time. */

    /* Get current buffer write index */
    uint8_t *wram = bus_get_wram();
    if (!wram) return;
    int buf_pos = wram[0x004A];

    /* Build DMA entries (one per "row", cnt iterations) */
    uint16_t vram_cur = vram_base;
    uint16_t src_cur = src_addr;

    /* Debug: log first staging for each slot */
    {
        static uint8_t logged[8] = {0};
        if (!logged[slot_idx]) {
            printf("smk: DMA stage slot %d: frame=$%04X tile=$%02X cnt=%d src=$%02X:%04X vram=$%04X size=$%02X\n",
                   slot_idx, frame_data, tile_idx, cnt, (uint8_t)(config & 0xFF), src_addr, vram_base, dma_size);
            logged[slot_idx] = 1;
        }
    }

    for (int j = 0; j < cnt; j++) {
        /* Entry: 3 words (6 bytes) at $0EA0 + buf_pos */
        int base = 0x0EA0 + buf_pos;
        wram[base + 0] = (uint8_t)(vram_cur & 0xFF);
        wram[base + 1] = (uint8_t)(vram_cur >> 8);
        wram[base + 2] = (uint8_t)(src_cur & 0xFF);
        wram[base + 3] = (uint8_t)(src_cur >> 8);
        wram[base + 4] = (uint8_t)(config & 0xFF);  /* bank */
        wram[base + 5] = dma_size;                   /* DMA size low byte */
        buf_pos += 6;

        vram_cur += 0x0100;  /* next VRAM row */
        src_cur += 0x0200;   /* next source row */
    }

    /* Update buffer index */
    wram[0x004A] = (uint8_t)buf_pos;
}

static void smk_build_oam_from_slots(void) {
    static const uint16_t slots[8] = {
        0x1000, 0x1100, 0x1200, 0x1300,
        0x1400, 0x1500, 0x1600, 0x1700
    };

    uint8_t *wram = bus_get_wram();
    if (!wram) return;

    /* Build DMA staging entries for any slots with changed frame data */
    for (int i = 0; i < 8; i++) {
        uint16_t phase = slot_read(slots[i], 0x42);
        if (phase == 0) continue;
        smk_build_dma_staging(i);
    }

    /* Clear OAM before building — set all 128 sprites offscreen */
    for (int j = 0; j < 0x200; j += 4) {
        wram[0x0200 + j + 0] = 0xF8;  /* X = 248 */
        wram[0x0200 + j + 1] = 0xE0;  /* Y = 224 (offscreen) */
        wram[0x0200 + j + 2] = 0x00;  /* tile = 0 */
        wram[0x0200 + j + 3] = 0x00;  /* attr = 0 */
    }
    memset(wram + 0x0400, 0, 0x20);  /* clear high table */

    int oam_idx = 0;

    for (int i = 0; i < 8; i++) {
        uint16_t s = slots[i];
        uint16_t phase = slot_read(s, 0x42);
        if (phase == 0) continue;  /* inactive */

        /* Screen position from slot $18/$1C.
         * Title screen uses screen-relative coordinates directly.
         * Race mode would subtract camera offset ($80:CF0E uses + $FF00). */
        uint16_t pos_x = bus_wram_read16(s + 0x18);
        uint16_t pos_y = bus_wram_read16(s + 0x1C);
        uint16_t sx = pos_x;
        uint16_t sy = pos_y;

        /* Skip if clearly offscreen (X > 255 and not wrapping) */
        if (sx > 0x00FF && sx < 0xFF00) continue;

        /* Compute OAM tile from VRAM dest for this slot.
         * VRAM dest is in the second sprite page (objTileAdr2=$5000).
         * Tile index in nt=1 space = (vram_dest - $5000) / 16.
         * OAM name table bit = 1.
         */
        uint16_t vram_dest = vram_dest_table[i];
        uint8_t tile = (uint8_t)((vram_dest - 0x5000) / 16);

        /* Build OAM attr: nt=1, pal from $2A high byte bits 1-3,
         * priority from $2A high byte bits 4-5 */
        uint16_t tile_attr = slot_read(s, 0x2A);
        uint8_t orig_attr = (uint8_t)(tile_attr >> 8);
        /* Keep palette and priority from $2A, set nt=1 */
        uint8_t attr = (orig_attr & 0xFE) | 0x01;  /* set nt=1 */

        /* $86,x bit 15 = H-flip */
        uint16_t flags = slot_read(s, 0x86);
        if (flags & 0x8000) attr ^= 0x40;

        /* Single 16×16 OAM entry per slot. The per-frame DMA updates
         * one 32-byte tile at the VRAM base. The other 3 sub-tiles of the
         * 16×16 sprite come from the init load ($7F:A000 → VRAM $4000). */
        uint16_t oam_addr = 0x0200 + oam_idx * 4;
        wram[oam_addr + 0] = (uint8_t)(sx & 0xFF);
        wram[oam_addr + 1] = (uint8_t)(sy & 0xFF);
        wram[oam_addr + 2] = tile;
        wram[oam_addr + 3] = attr;

        /* OAM high table: size=large (16×16), X bit 8 = 0 */
        uint16_t hi_addr = 0x0400 + (oam_idx / 4);
        uint8_t hi_shift = (oam_idx % 4) * 2;
        uint8_t hi_bits = 0x02;  /* size=large */
        wram[hi_addr] = (wram[hi_addr] & ~(0x03 << hi_shift)) | (hi_bits << hi_shift);

        oam_idx++;
    }
}

/*
 * $85:8045 — Per-frame sprite update
 *
 * Called from $80:80BA each frame during the title screen.
 * Mode dispatch: mode 0 builds OAM, mode 1 handles input.
 */
void smk_858045(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);


    /* JSL $81BB70 — RNG (skip for now) */
    /* JSR $92F9 — input handling (copies joypad when brightness ready) */

    /* LDA $7B / BNE skip */
    uint16_t v7b = bus_wram_read16(g_cpu.DP + 0x7B);
    if (v7b != 0) {
        g_cpu.DB = saved_db;
        return;
    }

    /* Mode dispatch: LDA $80 / AND #$07 / ASL / TAX / JMP ($839B,x) */
    uint8_t mode = bus_wram_read8(g_cpu.DP + 0x80) & 0x07;

    if (mode == 0) {
        /* Mode 0: JSR $84D1 (INC $64) */
        uint16_t v64 = bus_wram_read16(g_cpu.DP + 0x64);
        bus_wram_write16(g_cpu.DP + 0x64, v64 + 1);

        /* REP #$30 / LDA #$0200 / STA $3C */
        op_rep(0x30);
        bus_wram_write16(g_cpu.DP + 0x3C, 0x0200);

        /* JSR $8B7A — animation state machine */
        smk_85_8B7A();

        /* $80:FE07 — update slot positions (velocity → position) */
        smk_update_slot_positions();

        /* JSL $81CB44 — linked list OAM builder (simplified) */
        smk_build_oam_from_slots();
    }

    /* REP #$30 / LDA $80 / BNE skip / JSL $84FD25 */
    op_rep(0x30);
    if (mode == 0) {
        smk_84FD25();
    }

    g_cpu.DB = saved_db;
}

/*
 * $84:FD25 — Save data erase menu handler
 *
 * When $7B == 0: checks if Y+A+L+R buttons are pressed ($0020|$0022 == $40B0).
 *   If pressed: enters erase mode ($7B=1), copies erase text sprites to $03D0.
 *   Otherwise: does nothing and returns.
 * When $7B != 0: handles cursor movement (up/down) and confirm (A/Start).
 *   $7B=1: cursor on YES, $7B=2: cursor on NO.
 *   Confirming at $7B=2 erases SRAM $30:6000-$67FF.
 *
 * Since we have no joypad input yet, $7B stays 0 and this is effectively a no-op.
 */
void smk_84FD25(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x84);
    op_rep(0x30);

    uint16_t menu_state = bus_wram_read16(g_cpu.DP + 0x7B);
    if (menu_state != 0) {
        /* TODO: handle cursor movement and erase confirm when joypad is wired */
        g_cpu.DB = saved_db;
        return;
    }

    /* Check button combo Y+A+L+R ($40B0) to enter erase mode */
    uint16_t joy1 = bus_wram_read16(0x0020);
    uint16_t joy2 = bus_wram_read16(0x0022);
    if ((joy1 | joy2) == 0x40B0) {
        /* Enter erase mode — copy 12 erase text OAM entries from ROM $84:FDEA */
        bus_wram_write16(g_cpu.DP + 0x7B, 1);
        uint8_t *wram = bus_get_wram();
        if (wram) {
            for (int i = 0; i < 48; i += 2) {
                uint16_t w = bus_read16(0x84, 0xFDEA + i);
                wram[0x03D0 + i] = (uint8_t)(w & 0xFF);
                wram[0x03D0 + i + 1] = (uint8_t)(w >> 8);
            }
        }
    }

    g_cpu.DB = saved_db;
}

/* ===================================================================
 * Mode select graphics loading chain ($81:E627)
 *
 * Called during state $14 (mode select) transition to load all BG
 * tile data, tilemaps, and palettes into VRAM for Mode 0 display.
 *
 * E627 → E631 (BG graphics) → E63F (palettes) → E72E (BG config)
 * =================================================================== */

/*
 * $81:E84C — Compute 3-byte table index from $0126
 * Returns X = ($0126 >> 1) + $0126 = 1.5 × $0126
 */
static uint16_t compute_idx_0126(void) {
    uint16_t val = bus_wram_read16(0x0126);
    return (val >> 1) + val;
}

/*
 * $81:E64A — Compute 3-byte table index from $0124
 * Returns X = ($0124 << 1) + $0124 = 3 × $0124
 */
static uint16_t compute_idx_0124(void) {
    uint16_t val = bus_wram_read16(0x0124);
    return (val << 1) + val;
}

/*
 * $81:E836 — DMA helper: transfer from $7F:src to VRAM
 *
 * Entry: A(vram_addr) = VRAM destination, Y(src_off) = source offset in $7F,
 *        X(size) = transfer size.
 * DMA ctrl/dest must be pre-configured in $4300/$4301 by caller.
 */
static void dma_helper_e836(uint16_t vram_addr, uint16_t src_off, uint16_t size) {
    /* STY $4302 — source address low/mid */
    bus_write16(0x81, 0x4302, src_off);
    /* STA $2116 — VRAM address */
    bus_write16(0x81, 0x2116, vram_addr);
    /* LDA #$007F; STA $4304 — source bank $7F */
    bus_write16(0x81, 0x4304, 0x007F);
    /* STX $4305 — transfer size */
    bus_write16(0x81, 0x4305, size);
    /* Trigger DMA ch0 */
    bus_write16(0x81, 0x420B, 0x0001);
}

/*
 * $84:E378 — Post-processing nibble expansion
 *
 * Expands packed 4-bit pixel data into 8-bit color-indexed bytes.
 * For each row: palette byte from source[row], pixel data from source+$100.
 * Each packed byte → 2 output bytes (low nibble, high nibble), ORed with palette.
 *
 * Entry: X = source base offset in $7F, Y = dest base offset in $7F,
 *        A = row count
 */
void smk_84E378(uint16_t src_base, uint16_t dst_base, uint16_t row_count) {
    uint8_t *wram = bus_get_wram();
    if (!wram) return;

    uint8_t *buf = wram + 0x10000;  /* $7F bank */
    uint16_t out_y = 0;

    for (uint16_t row = 0; row < row_count; row++) {
        /* Palette byte from source[row] (via indirect long [$06],Y) */
        uint8_t palette = buf[(uint16_t)(src_base + row)];

        /* Pixel data starts at source + $0100 + row*32 */
        uint16_t px_base = (uint16_t)(src_base + 0x0100 + row * 32);

        for (int col = 0; col < 32; col++) {
            uint8_t packed = buf[(uint16_t)(px_base + col)];

            /* Low nibble */
            uint8_t lo = packed & 0x0F;
            buf[(uint16_t)(dst_base + out_y)] = (lo != 0) ? (lo | palette) : 0;
            out_y++;

            /* High nibble */
            uint8_t hi = (packed >> 4) & 0x0F;
            buf[(uint16_t)(dst_base + out_y)] = (hi != 0) ? (hi | palette) : 0;
            out_y++;
        }
    }
    printf("smk: E378 nibble expand $7F:%04X → $7F:%04X (%d rows, %d bytes)\n",
           src_base, dst_base, row_count, out_y);
}

/*
 * $81:E6B9 — Decompress $C4:0000 + post-process
 *
 * 1. Decompress from $C4:0000 → $7F:0000 (via $84:E09E)
 * 2. Post-process nibble expand → $7F:7000 (via $84:E378)
 */
static void sub_e6b9(void) {
    /* Decompress $C4:0000 → $7F:0000 */
    g_cpu.Y = 0x0000;
    CPU_SET_A16(0x00C4);
    g_cpu.X = 0x0000;
    smk_84E09E();

    /* Post-process: X=0, Y=$7000, A=$40 (64 rows) */
    smk_84E378(0x0000, 0x7000, 0x0040);
}

/*
 * $81:E68E — Decompress + DMA to VRAM $3000
 *
 * Calls E6B9 (decompress $C4:0000 → $7F:0000, then nibble-expand → $7F:7000),
 * then DMAs $7F:7000 → VRAM $3000 (4KB, high bytes via VMDATAH).
 *
 * Register chaining: STA $4301 with $0019 sets $4301=$19,$4302=$00.
 * Then STA $4303 with $7F70 sets $4303=$70,$4304=$7F.
 * Source addr = $4302|($4303<<8) = $0000|($70<<8) = $7000. Bank=$7F.
 */
static void sub_e68e(void) {
    sub_e6b9();

    op_rep(0x30);
    bus_write16(0x81, 0x2115, 0x0080);  /* VMAIN=$80 */
    bus_write16(0x81, 0x2116, 0x3000);  /* VMADD=$3000 */
    bus_write16(0x81, 0x4300, 0x0000);  /* ctrl=$00 */
    bus_write16(0x81, 0x4301, 0x0019);  /* dest=$19, src_lo=$00 */
    bus_write16(0x81, 0x4303, 0x7F70);  /* src_hi=$70, bank=$7F → $7F:7000 */
    bus_write16(0x81, 0x4305, 0x1000);  /* size=4KB */
    bus_write16(0x81, 0x420B, 0x0001);
}

/*
 * $81:E6D4 — Multi-bank graphics decompression
 *
 * Decompresses 3-4 graphics blocks from various ROM banks using
 * lookup tables indexed by $0126. Includes nibble expansion step.
 */
static void sub_e6d4(void) {
    uint16_t x;

    /* === Block 1: table at $81:EBA3 === */
    x = compute_idx_0126();
    {
        uint16_t y_val = bus_read16(0x81, 0xEBA3 + x);     /* VRAM/dest offset */
        uint16_t bank_raw = bus_read16(0x81, 0xEBA5 + x);
        uint8_t bank = bank_raw & 0xFF;

        /* Decompress from bank:$C000 → $7F:C000 */
        g_cpu.Y = y_val;
        CPU_SET_A16(bank);
        g_cpu.X = 0xC000;
        smk_84E09E();
    }

    /* Post-process: nibble expand $7F:C000 → $7F:4000 (192 rows) */
    smk_84E378(0xC000, 0x4000, 0x00C0);

    /* === Block 2: table at $81:EBEB === */
    x = compute_idx_0126();
    {
        uint16_t y_val = bus_read16(0x81, 0xEBEB + x);
        uint16_t bank_raw = bus_read16(0x81, 0xEBED + x);
        uint8_t bank = bank_raw & 0xFF;

        g_cpu.Y = y_val;
        CPU_SET_A16(bank);
        g_cpu.X = 0xC800;
        smk_84E09E();
    }

    /* === Block 3: fixed — $C1:12F8 → $7F:D000 === */
    g_cpu.Y = 0x12F8;
    CPU_SET_A16(0x00C1);
    g_cpu.X = 0xD000;
    smk_84E09E();

    /* === Block 4: table at $81:EC03 === */
    x = compute_idx_0126();
    {
        uint16_t y_val = bus_read16(0x81, 0xEC03 + x);
        uint16_t bank_raw = bus_read16(0x81, 0xEC05 + x);
        uint8_t bank = bank_raw & 0xFF;

        g_cpu.Y = y_val;
        CPU_SET_A16(bank);
        g_cpu.X = 0xC000;
        smk_84E09E();
    }

    printf("smk: E6D4 multi-bank decompression complete\n");
}

/*
 * $81:E7DA — Tilemap DMA helper #1
 * DMA $7F:C000 (1536 bytes, mode 1) → VRAM $7800
 *
 * Original: LDX #$0600, LDA #$7800, LDY #$C000, JSR E836
 * E836: Y→$4302 (src), A→$2116 (VRAM), X→$4305 (size)
 */
static void sub_e7da(void) {
    bus_write16(0x81, 0x2115, 0x0080);  /* VMAIN=$80 */
    bus_write16(0x81, 0x4300, 0x1801);  /* ctrl=$01 (mode 1), dest=$18 */
    bus_write16(0x81, 0x4302, 0x0000);  /* clear low */
    dma_helper_e836(0x7800, 0xC000, 0x0600);
}

/*
 * $81:E7F6 — Tilemap DMA helper #2
 * 4 sequential DMA transfers of tilemap fragments:
 *
 * Original parameters (LDX=size, LDA=VRAM, LDY=src):
 *   $7F:C100 (768B) → VRAM $7C00
 *   $7F:C000 (256B) → VRAM $7D80
 *   $7F:C500 (256B) → VRAM $7E00
 *   $7F:C400 (256B) → VRAM $7E80
 */
static void sub_e7f6(void) {
    bus_write16(0x81, 0x2115, 0x0080);
    bus_write16(0x81, 0x4300, 0x1801);  /* mode 1 */
    bus_write16(0x81, 0x4302, 0x0000);

    dma_helper_e836(0x7C00, 0xC100, 0x0300);
    dma_helper_e836(0x7D80, 0xC000, 0x0100);
    dma_helper_e836(0x7E00, 0xC500, 0x0100);
    dma_helper_e836(0x7E80, 0xC400, 0x0100);
}

/*
 * $81:E769 — Tilemap DMA chain
 *
 * Two main DMA transfers + two helpers:
 * 1. $7F:4000 (12KB high bytes) → VRAM $0000
 * 2. $7F:C800 (4KB mode 1) → VRAM $7000
 * 3. E7DA: $7F:C000 → VRAM $7800
 * 4. E7F6: fragments → VRAM $7C00-$7E80
 *
 * Note on register chaining: 16-bit writes to $43XX set consecutive bytes.
 * Source address = $4302(lo) | $4303(hi)<<8, bank = $4304.
 */
static void sub_e769(void) {
    /* DMA #1: $7F:4000 → VRAM $0000, 12KB, high bytes only
     * Original: STZ $4300 ($4300=$00,$4301=$00)
     *           LDA #$0019; STA $4301 ($4301=$19,$4302=$00)
     *           LDA #$7F40; STA $4303 ($4303=$40,$4304=$7F)
     *           → src = $7F:4000, dest = VMDATAH */
    bus_write16(0x81, 0x2115, 0x0080);  /* VMAIN=$80 */
    bus_write16(0x81, 0x2116, 0x0000);  /* VMADD=$0000 */
    bus_write16(0x81, 0x4300, 0x0000);  /* ctrl=$00, dest=$00 (overwritten next) */
    bus_write16(0x81, 0x4301, 0x0019);  /* dest=$19 (VMDATAH), src_lo=$00 */
    bus_write16(0x81, 0x4303, 0x7F40);  /* src_hi=$40, bank=$7F → $7F:4000 */
    bus_write16(0x81, 0x4305, 0x3000);  /* size=12KB */
    bus_write16(0x81, 0x420B, 0x0001);

    /* DMA #2: $7F:C800 → VRAM $7000, 4KB, mode 1 (VMDATAL+VMDATAH)
     * Original: LDA #$1801; STA $4300 ($4300=$01,$4301=$18)
     *           STZ $4302 ($4302=$00,$4303=$00)
     *           LDA #$7FC8; STA $4303 ($4303=$C8,$4304=$7F)
     *           → src = $7F:C800 */
    bus_write16(0x81, 0x2116, 0x7000);  /* VMADD=$7000 */
    bus_write16(0x81, 0x4300, 0x1801);  /* ctrl=$01 (mode 1), dest=$18 */
    bus_write16(0x81, 0x4302, 0x0000);  /* clear src_lo, src_hi */
    bus_write16(0x81, 0x4303, 0x7FC8);  /* src_hi=$C8, bank=$7F → $7F:C800 */
    bus_write16(0x81, 0x4305, 0x1000);  /* size=4KB */
    bus_write16(0x81, 0x420B, 0x0001);

    /* DMA #3-#7: tilemap fragments */
    sub_e7da();
    sub_e7f6();

    printf("smk: E769 tilemap DMAs complete\n");
}

/*
 * $81:E745 — Palette data decompression
 *
 * Double-decompression: ROM → $7F:C000 → re-decompress → $7F:0000
 * Uses table at $81:EB5B indexed by $0124.
 */
static void sub_e745(void) {
    uint16_t x = compute_idx_0124();

    /* First decompress: table lookup → $7F:C000 */
    {
        uint16_t y_val = bus_read16(0x81, 0xEB5B + x);
        uint16_t bank_raw = bus_read16(0x81, 0xEB5D + x);
        uint8_t bank = bank_raw & 0xFF;

        g_cpu.Y = y_val;
        CPU_SET_A16(bank);
        g_cpu.X = 0xC000;
        smk_84E09E();
    }

    /* Second decompress: $7F:C000 → $7F:0000
     * Source = $7F:C000 (the just-decompressed data), dest = $7F:0000 */
    g_cpu.Y = 0xC000;
    CPU_SET_A16(0x007F);
    g_cpu.X = 0x0000;
    smk_84E09E();

    printf("smk: E745 palette double-decompression complete\n");
}

/*
 * $81:E7B5 — DMA palette/tile data to VRAM low bytes
 *
 * DMA $7F:0000 (16KB) → VRAM $0000 (low bytes, VMDATAL)
 * VMAIN=$0000: increment after low byte write
 */
static void sub_e7b5(void) {
    bus_write16(0x81, 0x4300, 0x0000);  /* ctrl=$00 (1-reg) */
    bus_write16(0x81, 0x2115, 0x0000);  /* VMAIN=$00 (inc after low) */
    bus_write16(0x81, 0x2116, 0x0000);  /* VMADD=$0000 */
    bus_write16(0x81, 0x4301, 0x0018);  /* dest=$18 (VMDATAL) */
    bus_write16(0x81, 0x4303, 0x7F00);  /* src=$7F:0000 */
    bus_write16(0x81, 0x4305, 0x4000);  /* size=16KB */
    bus_write16(0x81, 0x420B, 0x0001);
}

/*
 * $84:DF48 — BG configuration data decompressor
 *
 * Reads a compressed data stream from ROM and decompresses to $7E bank (WRAM).
 * Same compression format as $84:E09E but targets $7E:XXXX instead of $7F:XXXX.
 *
 * Entry: DP $0E = output start, DP $10 = data pointer, DB = data bank
 */
void sub_df48(uint16_t out_start, uint16_t data_ptr, uint8_t data_bank) {
    uint8_t *wram = bus_get_wram();
    if (!wram) return;

    uint16_t out_pos = out_start;
    uint16_t src_pos = data_ptr;

    while (1) {
        uint8_t cmd = bus_read8(data_bank, src_pos++);
        if (cmd == 0xFF) break;

        uint8_t mode;
        uint16_t count;

        if ((cmd & 0xE0) == 0xE0) {
            /* Extended command: mode from cmd<<3, 10-bit count */
            mode = (cmd << 3) & 0xE0;
            uint8_t lo = bus_read8(data_bank, src_pos++);
            count = (lo | ((uint16_t)(cmd & 0x03) << 8)) + 1;
        } else {
            mode = cmd & 0xE0;
            count = (cmd & 0x1F) + 1;
        }

        switch (mode) {
        case 0x00:
            /* Raw copy from stream to output */
            for (uint16_t i = 0; i < count; i++) {
                wram[out_pos++] = bus_read8(data_bank, src_pos++);
            }
            break;

        case 0x20:
            /* RLE fill */
            {
                uint8_t fill = bus_read8(data_bank, src_pos++);
                for (uint16_t i = 0; i < count; i++) {
                    wram[out_pos++] = fill;
                }
            }
            break;

        case 0x40:
            /* Word fill: alternate 2 bytes */
            {
                uint8_t b0 = bus_read8(data_bank, src_pos++);
                uint8_t b1 = bus_read8(data_bank, src_pos++);
                for (uint16_t i = count; i > 0; ) {
                    wram[out_pos++] = b0; i--;
                    if (i == 0) break;
                    wram[out_pos++] = b1; i--;
                }
            }
            break;

        case 0x60:
            /* Incrementing fill */
            {
                uint8_t val = bus_read8(data_bank, src_pos++);
                for (uint16_t i = 0; i < count; i++) {
                    wram[out_pos++] = val++;
                }
            }
            break;

        case 0x80:
            /* Back-reference: 16-bit offset + out_start */
            {
                uint16_t ref = bus_read8(data_bank, src_pos);
                ref |= (uint16_t)bus_read8(data_bank, src_pos + 1) << 8;
                src_pos += 2;
                ref += out_start;
                for (uint16_t i = 0; i < count; i++) {
                    wram[out_pos] = wram[ref];
                    out_pos++; ref++;
                }
            }
            break;

        case 0xA0:
            /* Back-reference with XOR */
            {
                uint16_t ref = bus_read8(data_bank, src_pos);
                ref |= (uint16_t)bus_read8(data_bank, src_pos + 1) << 8;
                src_pos += 2;
                ref += out_start;
                for (uint16_t i = 0; i < count; i++) {
                    wram[out_pos] = wram[ref] ^ 0xFF;
                    out_pos++; ref++;
                }
            }
            break;

        case 0xC0:
        case 0xE0:
            /* Byte-offset back-reference */
            {
                uint8_t offset = bus_read8(data_bank, src_pos++);
                uint16_t ref = out_pos - offset;
                bool invert = (mode == 0xE0);
                for (uint16_t i = 0; i < count; i++) {
                    uint8_t val = wram[ref++];
                    wram[out_pos++] = invert ? (val ^ 0xFF) : val;
                }
            }
            break;
        }
    }

    printf("smk: DF48 decompress $%02X:%04X → $7E:%04X (size=%04X)\n",
           data_bank, data_ptr, out_start, out_pos - out_start);
}

/*
 * $81:E631 — BG tilemap/graphics loading chain
 *
 * Calls: E68E (decompress+DMA), E6D4 (multi-bank), E769 (tilemap DMAs),
 *        $83:F2C3 (mode-specific dispatch)
 */
static void sub_e631(void) {
    sub_e68e();
    sub_e6d4();
    sub_e769();

    /* $83:F2C3 — mode-specific tile loading dispatcher
     * Uses table at $83:F000 indexed by $0126.
     * Stub for now — loads additional mode-specific graphics. */
    {
        uint16_t idx = bus_wram_read16(0x0126);
        printf("smk: $83:F2C3 stub — mode-specific tile loader ($0126=%04X)\n", idx);
    }
}

/*
 * $81:E63F — Palette and tile graphics chain
 *
 * Calls: E745 (palette decompress), $84:F147 (palette check), E7B5 (VRAM DMA)
 */
static void sub_e63f(void) {
    sub_e745();

    /* $84:F147 — palette bounds check (stub: just continue) */
    /* Original checks if DP $32 < $10, then does additional setup.
     * For initial mode select rendering, skip. */

    sub_e7b5();
}

/*
 * $81:E72E — BG layer configuration
 *
 * Reads config from table at $81:EBBB indexed by $0126,
 * decompresses BG config data to WRAM via $84:DF48.
 */
static void sub_e72e(void) {
    uint16_t x = compute_idx_0126();

    uint16_t y_val = bus_read16(0x81, 0xEBBB + x);      /* data pointer */
    uint16_t bank_raw = bus_read16(0x81, 0xEBBD + x);
    uint8_t bank = bank_raw & 0xFF;

    /* $84:DF38 → DF48: decompress data_bank:y_val → $7E:C000 */
    sub_df48(0xC000, y_val, bank);
}

/*
 * $81:E627 — Mode select graphics loading orchestrator
 *
 * Main entry point: loads all BG tile data, tilemaps, palettes,
 * and configuration for the mode select (state $14) screen.
 */
void smk_81E627(void) {
    printf("smk: E627 mode select graphics loading begin\n");

    sub_e631();   /* BG tilemap/graphics */
    sub_e63f();   /* Palettes + VRAM fill */
    sub_e72e();   /* BG config → decompresses CGRAM palette to $7E:C000 */

    /* E72E decompresses CGRAM palette to $7E:C000.
     * The actual CGRAM load happens in the transition handler via $8C1A's
     * palette ($C4:$1313 → $7E:$3A80 → CGRAM DMA). */


    printf("smk: E627 mode select graphics loading complete\n");
}

/* ===================================================================
 * Mode select screen (state $06) — bank $85
 * =================================================================== */

/*
 * $85:91DE — Mode select display setup
 *
 * Calls $84:F421 (viewport params), reads character selection tables,
 * sets up viewport split positions, calls $84:F45A (PPU Mode 0 setup).
 *
 * Original: JSL $84F421, table lookups based on $2E/$1012/$1112,
 *           sets DP $66/$68 viewport positions, JSL $84F45A
 */
void smk_8591DE(void) {
    /* JSL $84F421 — viewport/HDMA setup */
    smk_84F421();

    op_rep(0x30);

    uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2E);

    if (mode == 0) {
        /* 1P mode: check if P1 and P2 characters are the same */
        uint16_t p1_char = bus_wram_read16(0x1012);
        uint16_t p2_char = bus_wram_read16(0x1112);

        if (p1_char != p2_char) {
            /* Different characters — look up viewport positions from table */
            uint16_t pos1 = bus_read16(0x85, 0x9283 + p1_char);
            bus_wram_write16(g_cpu.DP + 0x66, pos1);
            uint16_t pos2 = bus_read16(0x85, 0x9283 + p2_char);
            bus_wram_write16(g_cpu.DP + 0x68, pos2);
        } else {
            /* Same character */
            bus_wram_write16(g_cpu.DP + 0x66, 0);
            bus_wram_write16(g_cpu.DP + 0x68, 0x0008);
        }
    } else {
        /* 2P/Battle mode */
        if (mode == 4) {
            uint16_t p2_char = bus_wram_read16(0x1112);
            uint16_t pos = bus_read16(0x85, 0x9283 + p2_char);
            bus_wram_write16(g_cpu.DP + 0x66, pos);
        } else {
            uint16_t p1_char = bus_wram_read16(0x1012);
            uint16_t pos = bus_read16(0x85, 0x9283 + p1_char);
            bus_wram_write16(g_cpu.DP + 0x66, pos);
        }
        /* Set second viewport for split screen */
        bus_wram_write16(g_cpu.DP + 0x68, 0x00F0);
        bus_wram_write16(g_cpu.DP + 0x70 + 2, 0x0002);
    }

    /* JSL $84F45A — PPU Mode 0 register setup */
    smk_84F45A();
}

/*
 * $85:915F — Tile DMA + palette loading
 *
 * DMAs 4 tile blocks from WRAM $7F to VRAM (same tables as title screen),
 * fills VRAM $3800 with background pattern, loads palette from $7F:E800.
 *
 * Original:
 *   LDX #$08/$0A/$0C/$0E, JSR $8171 (×4) — DMA tile blocks
 *   Fill VRAM $3800 with $3014 (1024 words)
 *   Load 256 CGRAM colors from $7F:E800
 *   If $2E != 0, JSR $91B4 + $91BB
 */
void smk_85915F(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);
    op_sep(0x30);

    /* DMA 4 tile blocks: X = $08, $0A, $0C, $0E
     * Tables at $85:81AA (src), $85:81D0 (size), $85:81F6 (vram dest) */
    for (int blk = 0; blk < 4; blk++) {
        int x = 0x08 + blk * 2;

        bus_write8(0x85, 0x4300, 0x01);  /* DMA ctrl: 2-reg word write */
        bus_write8(0x85, 0x4301, 0x18);  /* B-bus: $2118 (VMDATAL) */

        uint8_t src_lo = bus_read8(0x85, 0x81AA + x);
        uint8_t src_hi = bus_read8(0x85, 0x81AB + x);
        bus_write8(0x85, 0x4302, src_lo);
        bus_write8(0x85, 0x4303, src_hi);
        bus_write8(0x85, 0x4304, 0x7F);  /* source bank $7F */

        uint8_t size_lo = bus_read8(0x85, 0x81D0 + x);
        uint8_t size_hi = bus_read8(0x85, 0x81D1 + x);
        bus_write8(0x85, 0x4305, size_lo);
        bus_write8(0x85, 0x4306, size_hi);

        uint8_t vram_lo = bus_read8(0x85, 0x81F6 + x);
        uint8_t vram_hi = bus_read8(0x85, 0x81F7 + x);
        bus_write8(0x85, 0x2116, vram_lo);
        bus_write8(0x85, 0x2117, vram_hi);

        bus_write8(0x85, 0x420B, 0x01);  /* trigger DMA ch0 */
    }

    op_rep(0x30);

    /* Fill VRAM at $3800 with $3014 pattern (1024 words) */
    bus_write16(0x85, 0x2116, 0x3800);
    for (int i = 0; i < 0x400; i++) {
        bus_write8(0x00, 0x2118, 0x14);
        bus_write8(0x00, 0x2119, 0x30);
    }

    /* Load 256 CGRAM colors from $7F:E800 */
    op_sep(0x20);
    bus_write8(0x85, 0x2121, 0x00);  /* CGRAM address = 0 */

    uint8_t *wram = bus_get_wram();
    if (wram) {
        for (int i = 0; i < 0x200; i++) {
            bus_write8(0x85, 0x2122, wram[0x10000 + 0xE800 + i]);
        }
    }

    op_rep(0x20);

    /* If $2E != 0 (2P mode), additional palette setup */
    uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2E);
    if (mode != 0) {
        /* JSR $91B4 — additional palette loading (stub) */
        /* JSR $91BB — additional setup (stub) */
    }

    op_sep(0x20);
    g_cpu.DB = saved_db;
}

/*
 * $85:9239 — HDMA table builder / sprite slot init wrapper
 *
 * Loads sprite init table pointer and calls $81:CB98.
 *
 * Original:
 *   LDY #$92A3     ; table at $85:92A3
 *   JSL $81CB98    ; HDMA/sprite slot builder
 *   RTS
 */
void smk_859239(void) {
    /* The sprite init table at $85:92A3 defines 8 sprite slots
     * with X/Y positions and tile attributes.
     * $81:CB98 reads this table and initializes each slot.
     * For now, implement basic slot initialization inline. */

    op_rep(0x30);

    /* Initialize basic sprite slot data from table at $85:92A3 */
    static const struct {
        uint16_t slot_base;
        uint16_t x_pos;
        uint16_t y_pos;
        uint16_t tile_attr;
    } slots[8] = {
        { 0x1000, 0x0038, 0x0070, 0x4000 },
        { 0x1100, 0x0038, 0x00B1, 0x4000 },
        { 0x1200, 0x0098, 0x0070, 0x4000 },
        { 0x1300, 0x0068, 0x0071, 0x4000 },
        { 0x1400, 0x0098, 0x00B1, 0x4000 },
        { 0x1500, 0x00C8, 0x0071, 0x4000 },
        { 0x1600, 0x00C8, 0x00B0, 0x4000 },
        { 0x1700, 0x0068, 0x00B0, 0x4000 },
    };

    /* Set common init values (from $81:CB98) */
    bus_wram_write16(g_cpu.DP + 0xC8, 0x1000);
    bus_wram_write16(g_cpu.DP + 0x4A, 0);
    bus_wram_write16(0x1E92, 0);
    bus_wram_write16(g_cpu.DP + 0xB0, 0x00B0);
    bus_wram_write16(g_cpu.DP + 0xB2, 0x00B0);
    bus_wram_write16(0x1EF0, 0);

    uint8_t *wram = bus_get_wram();
    if (!wram) return;

    for (int i = 0; i < 8; i++) {
        uint16_t base = slots[i].slot_base;

        /* Clear slot memory (80 words = 160 bytes) at slot+$00 and slot+$A0 */
        for (int j = 0; j < 0xA0; j += 2) {
            bus_wram_write16(base + j, 0);
        }

        /* Set position and attributes */
        bus_wram_write16(base + 0x18, slots[i].x_pos);
        bus_wram_write16(base + 0x1C, slots[i].y_pos);
        bus_wram_write16(base + 0x2A, slots[i].tile_attr);
        bus_wram_write16(base + 0xBA, 0x0140);
        bus_wram_write16(base + 0x30, 0x0120);
    }

    /* Initialize display variables for NMI rendering */
    uint16_t sel1 = bus_wram_read16(g_cpu.DP + 0x66);
    uint16_t sel2 = bus_wram_read16(g_cpu.DP + 0x68);
    uint16_t disp1 = bus_read16(0x85, 0x92E5 + sel1);
    uint16_t disp2 = bus_read16(0x85, 0x92E5 + sel2);

    bus_wram_write16(g_cpu.DP + 0x74, disp1);  /* P1 old display pos */
    bus_wram_write16(g_cpu.DP + 0x76, disp2);  /* P2 old display pos */
    bus_wram_write16(g_cpu.DP + 0x7E, disp1);  /* P1 new display pos */
    bus_wram_write16(g_cpu.DP + 0x80, disp2);  /* P2 new display pos */
    bus_wram_write16(g_cpu.DP + 0x70, 0);      /* P1 confirm state */
    bus_wram_write16(g_cpu.DP + 0x72, 0);      /* P2 confirm state */
    bus_wram_write16(g_cpu.DP + 0x96, 0);      /* Transition delay counter */
    bus_wram_write16(g_cpu.DP + 0x98, 0);      /* Animation counter */
    bus_wram_write16(g_cpu.DP + 0x9A, 0x1815); /* P1 cursor animation */
    bus_wram_write16(g_cpu.DP + 0x9C, 0x1C17); /* P2 cursor animation */

    /* Character sprite params */
    uint16_t spr1 = bus_read16(0x85, 0x9273 + sel1);
    uint16_t spr2 = bus_read16(0x85, 0x9273 + sel2);
    bus_wram_write16(g_cpu.DP + 0x8C, spr1);
    bus_wram_write16(g_cpu.DP + 0x8E, spr2);

    printf("smk: sprite slots initialized (8 slots, P1 sel=%d P2 sel=%d)\n",
           sel1/2, sel2/2);
}

/*
 * $85:909B — Mode select transition init
 *
 * Called from transition $06 handler ($81:E126).
 * Resets PPU, sets up display for mode selection screen.
 *
 * Original:
 *   PHB/PHK/PLB (DB=$85)
 *   SEP #$30
 *   JSL $84F38C   ; PPU reset
 *   JSR $91DE     ; mode select display setup
 *   JSR $915F     ; palette/tile loading
 *   REP #$30
 *   JSR $9239     ; OAM/sprite init
 *   PLB/RTL
 */
/*
 * $81:93FA — Direct VRAM write for portrait/sprite tiles
 *
 * Copies 32 words from $7F:X to VRAM A, then 32 words to VRAM A+$100.
 * Used by $81:CBE4 to load character portrait tiles.
 */
static void sub_93fa(uint16_t vram_dest, uint16_t src_offset) {
    /* Direct VRAM writes via $2118/$2119 (matches original $81:93FA).
     * bus_write8 to $2118/$2119 confirmed working during forced blank. */
    uint8_t *wram = bus_get_wram();
    if (!wram) return;
    uint8_t *buf = wram + 0x10000;  /* $7F bank */

    bus_write8(0x00, 0x2115, 0x80);  /* VMAIN=$80: inc after $2119 write */

    /* First row: 32 words → VRAM dest */
    bus_write8(0x00, 0x2116, (uint8_t)(vram_dest & 0xFF));
    bus_write8(0x00, 0x2117, (uint8_t)(vram_dest >> 8));
    for (int i = 0; i < 32; i++) {
        bus_write8(0x00, 0x2118, buf[src_offset]);      /* VMDATAL */
        bus_write8(0x00, 0x2119, buf[src_offset + 1]);   /* VMDATAH → inc */
        src_offset += 2;
    }

    /* Second row: 32 words → VRAM dest + $100 */
    uint16_t dest2 = vram_dest + 0x0100;
    bus_write8(0x00, 0x2116, (uint8_t)(dest2 & 0xFF));
    bus_write8(0x00, 0x2117, (uint8_t)(dest2 >> 8));
    for (int i = 0; i < 32; i++) {
        bus_write8(0x00, 0x2118, buf[src_offset]);
        bus_write8(0x00, 0x2119, buf[src_offset + 1]);
        src_offset += 2;
    }
}

/*
 * $81:CBE4 — Load character portrait tiles to VRAM
 *
 * Writes portrait tile data from WRAM $7F:8000+ to VRAM $5000+.
 * Three blocks: $5000/$5020/$5040, each with two 32-word rows.
 */
static void sub_cbe4(void) {
    /* Portrait tiles are in the 4bpp interleaved sprite data at $7F:A000
     * (prepared by E576 from $C7:0000 during boot).
     * Original CBE4 uses $8000/$8280/$8500 but that region contains a
     * structured table, not raw tile data. The actual sprite tile pixels
     * are at $7F:A000+ (16KB interleaved 4bpp data). */
    sub_93fa(0x5000, 0xA000);  /* $7F:A000 → VRAM $5000/$5100 */
    sub_93fa(0x5020, 0xA080);  /* $7F:A080 → VRAM $5020/$5120 */
    sub_93fa(0x5040, 0xA100);  /* $7F:A100 → VRAM $5040/$5140 */
}

void smk_85909B(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);

    op_sep(0x30);

    /* JSL $84F38C — PPU reset */
    smk_84F38C();

    /* JSR $91DE — mode select display setup */
    smk_8591DE();

    /* JSR $915F — tile DMA + palette loading */
    smk_85915F();

    op_rep(0x30);

    /* JSR $9239 — HDMA/sprite slot init */
    smk_859239();

    /* TODO: Portrait rendering needs more investigation.
     * Key findings:
     * - Direct VRAM writes via $2118/$2119 work during forced blank
     * - $7F:8000 ($C4:0594) contains structured table data, not raw tile pixels
     * - $7F:A000 ($C7:0000 interleaved) contains font/general sprites, not portraits
     * - Character face tiles are likely BG-based, loaded by $85:915F tile DMA
     * - CBE4 VRAM $5000 writes may be for UI elements, not character faces
     * - The full sprite builder at $85:95AD/$81:CB44 needs implementation */

    /* Set portrait draw flags to prevent NMI from overwriting face tiles
     * with placeholder patterns ($281C/$2410). The face tile data IS in
     * VRAM $3000 (loaded by $85:915F from $7F:EA00). The NMI handler at
     * $85:90D7 checks $0184/$0186 and skips the placeholder fill when non-zero. */
    bus_wram_write16(0x0184, 0x0001);
    bus_wram_write16(0x0186, 0x0001);

    g_cpu.DB = saved_db;
    printf("smk: character select init (state $06) complete\n");
}

/*
 * $85:93A9 — Determine active player index
 * Sets DP $82 = 0 (P1) or 2 (P2) based on which controller pressed.
 */
static void smk_85_93A9(void) {
    uint16_t e66 = bus_wram_read16(0x0E66);
    uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2E);

    if (e66 == 0 && mode != 0) {
        /* 2P mode, P2's turn */
        bus_wram_write16(g_cpu.DP + 0x82, 0);
    } else {
        uint16_t joy1 = bus_wram_read16(g_cpu.DP + 0x6A);
        if (joy1 != 0) {
            bus_wram_write16(g_cpu.DP + 0x82, 0);
        } else {
            bus_wram_write16(g_cpu.DP + 0x82, 2);
        }
    }
}

/*
 * $85:947D — Validate selection against other player
 * Swaps X with the other player index and compares.
 * Returns: sets Z flag if selection matches the other player's.
 */
static uint16_t smk_85_947D(uint16_t val, uint16_t x) {
    uint16_t other_x = x ^ 2;
    uint16_t other_sel = bus_wram_read16(g_cpu.DP + 0x66 + other_x);
    return val - other_sel;  /* 0 if match */
}

/*
 * $85:92F9 — Input processing
 * Copies edge-detected buttons to DP $6A/$6C.
 * For states $04/$06, skips button handler sub-call.
 */
static void smk_85_92F9(void) {
    /* Skip if fading */
    if (bus_wram_read16(g_cpu.DP + 0x48) != 0) return;

    /* Skip if not full brightness */
    op_sep(0x20);
    uint8_t brightness = bus_wram_read8(0x0161);
    op_rep(0x20);
    if (brightness != 0x0F) return;

    /* Copy edge-detected buttons to $6A/$6C */
    uint16_t joy1_edge = bus_wram_read16(g_cpu.DP + 0x28);
    bus_wram_write16(g_cpu.DP + 0x6A, joy1_edge);
    uint16_t joy2_edge = bus_wram_read16(g_cpu.DP + 0x2A);
    bus_wram_write16(g_cpu.DP + 0x6C, joy2_edge);

    /* For state $06, check if we need the button handler */
    uint16_t state = bus_wram_read16(g_cpu.DP + 0x36);
    if (state == 0x0004 || state == 0x0006) return;

    /* JSR $9336 — button handler for other states */
    {
        uint16_t mode = bus_wram_read16(0x002E);
        if (mode == 0) return;
        if (mode == 4) {
            bus_wram_write16(g_cpu.DP + 0x6A, 0);
        } else {
            bus_wram_write16(g_cpu.DP + 0x6C, 0);
        }
    }
}

/*
 * $85:9348 — Animation counter updates
 */
static void smk_85_9348(void) {
    uint16_t v1 = bus_wram_read16(0x0060);
    bus_wram_write16(0x0060, v1 + 0x00B0);

    uint16_t v2 = bus_wram_read16(g_cpu.DP + 0x63);
    bus_wram_write16(g_cpu.DP + 0x63, v2 + 0x0130);
}

/*
 * $85:93C4 — D-pad character selection handler
 * Navigates the 8-character grid (4×2) with D-pad.
 */
static void smk_85_93C4(void) {
    /* Determine active player */
    smk_85_93A9();

    uint16_t px = bus_wram_read16(g_cpu.DP + 0x82);

    /* Check lock-out */
    if (bus_wram_read16(0x0196 + px) != 0) return;
    if (bus_wram_read16(g_cpu.DP + 0x70 + px) != 0) return;

    /* Current selection */
    uint16_t sel = bus_wram_read16(g_cpu.DP + 0x66 + px);

    /* Save old display value */
    uint16_t old_disp = bus_read16(0x85, 0x92E5 + sel);
    bus_wram_write16(g_cpu.DP + 0x74 + px, old_disp);

    /* JSL $84F5F2 — stub (cursor display update) */

    /* Check D-pad direction */
    uint16_t dpad = (bus_wram_read16(g_cpu.DP + 0x6A) |
                     bus_wram_read16(g_cpu.DP + 0x6C)) & 0x0F00;

    uint16_t new_sel = sel;

    if (dpad == 0x0100) {
        /* Right: +2, wrap */
        new_sel = sel + 2;
        /* Skip matching other player's selection */
        while (new_sel < 16 && smk_85_947D(new_sel, px) == 0) {
            new_sel += 2;
        }
        if (new_sel >= 16) {
            /* Wrap to other row */
            new_sel = (sel < 8) ? 8 : 0;
            /* Find first non-matching */
            for (int tries = 0; tries < 8; tries++) {
                if (smk_85_947D(new_sel, px) != 0) break;
                new_sel += 2;
                if (new_sel >= 16) new_sel = 0;
            }
        }
    } else if (dpad == 0x0200) {
        /* Left: -2, wrap */
        new_sel = (sel >= 2) ? sel - 2 : 14;
        while (new_sel < 16 && smk_85_947D(new_sel, px) == 0) {
            new_sel = (new_sel >= 2) ? new_sel - 2 : 14;
        }
    } else if (dpad == 0x0800) {
        /* Up: -8 */
        new_sel = (sel >= 8) ? sel - 8 : sel;
        if (smk_85_947D(new_sel, px) == 0 || new_sel == sel) {
            /* Can't move up or blocked */
            goto done;
        }
    } else if (dpad == 0x0400) {
        /* Down: +8 */
        new_sel = (sel < 8) ? sel + 8 : sel;
        if (new_sel >= 16 || smk_85_947D(new_sel, px) == 0) {
            goto done;
        }
    } else {
        goto done;
    }

    /* Store new selection */
    if (new_sel < 16) {
        bus_wram_write16(g_cpu.DP + 0x66 + px, new_sel);

        /* Update display value */
        uint16_t new_disp = bus_read16(0x85, 0x92E5 + new_sel);
        bus_wram_write16(g_cpu.DP + 0x7E + px, new_disp);

        /* JSL $81F5A7 with A=$002C — play cursor move SFX */
        func_table_call(0x81F5A7);  /* may not be registered, OK */
    }

done:
    /* JSL $84F601 — stub (cursor redraw) */
    return;
}

/*
 * $85:939F — D-pad transition check
 * If D-pad pressed, dispatches to selection handler.
 */
static void smk_85_939F(void) {
    uint16_t buttons = bus_wram_read16(g_cpu.DP + 0x6A) |
                       bus_wram_read16(g_cpu.DP + 0x6C);
    if (buttons & 0x0F00) {
        smk_85_93C4();
    }
}

/*
 * $85:9487 — Confirm (B/Start) and Cancel (X) button handler
 */
static void smk_85_9487(void) {
    uint16_t buttons = bus_wram_read16(g_cpu.DP + 0x6A) |
                       bus_wram_read16(g_cpu.DP + 0x6C);

    if (buttons & 0x9000) {
        /* B ($8000) or Start ($1000) pressed — confirm selection */
        uint16_t mode = bus_wram_read16(0x002E);
        uint16_t active_x;

        if (mode == 4) {
            active_x = 0;
        } else {
            uint16_t joy1_b = bus_wram_read16(g_cpu.DP + 0x6A) & 0x9000;
            if (joy1_b) {
                active_x = 0;
            } else {
                active_x = 2;
            }
        }

        /* Check lock-out */
        if (bus_wram_read16(0x0196 + active_x) != 0) return;

        uint16_t confirm_state = bus_wram_read16(g_cpu.DP + 0x70 + active_x);
        if (confirm_state == 1) {
            /* Already in confirm animation, advance to fully confirmed */
            bus_wram_write16(g_cpu.DP + 0x70 + active_x, 2);
            return;
        }
        if (confirm_state == 2) return;  /* Already confirmed */

        /* Set confirm state */
        bus_wram_write16(0x0180 + active_x, active_x);
        bus_wram_write16(g_cpu.DP + 0x70 + active_x, 1);

        /* Look up sprite params */
        uint16_t sel = bus_wram_read16(g_cpu.DP + 0x66 + active_x);
        uint16_t sprite_val = bus_read16(0x85, 0x9273 + sel);
        bus_wram_write16(g_cpu.DP + 0x8C + active_x, sprite_val);
        bus_wram_write16(0x0184 + active_x, 0);

        /* Play confirm SFX — JSL $81F5A7 */
        func_table_call(0x81F5A7);
        return;
    }

    if (buttons & 0x0040) {
        /* X pressed — cancel selection */
        uint16_t mode = bus_wram_read16(0x002E);
        uint16_t active_x;

        if (mode == 4) {
            active_x = 0;
        } else {
            uint16_t joy1_x = bus_wram_read16(g_cpu.DP + 0x6A) & 0x0040;
            if (joy1_x) {
                active_x = 0;
            } else {
                active_x = 2;
            }
        }

        /* Can only cancel if in confirm state 1 */
        if (bus_wram_read16(g_cpu.DP + 0x70 + active_x) != 1) return;

        /* Cancel */
        bus_wram_write16(0x0180 + active_x, 8);
        bus_wram_write16(g_cpu.DP + 0x70 + active_x, 0);

        uint16_t sel = bus_wram_read16(g_cpu.DP + 0x66 + active_x);
        uint16_t sprite_val = bus_read16(0x85, 0x9273 + sel);
        bus_wram_write16(g_cpu.DP + 0x8C + active_x, sprite_val);
        bus_wram_write16(0x0184 + active_x, 2);

        /* Play cancel SFX */
        func_table_call(0x81F5A7);
    }
}

/*
 * $85:965B — Transition trigger
 * When both players have confirmed (after 64-frame delay), triggers
 * transition to the next game state.
 */
static void smk_85_965B(void) {
    /* Check both players confirmed ($70=2, $72=2) */
    if (bus_wram_read16(g_cpu.DP + 0x70) != 2) return;
    if (bus_wram_read16(g_cpu.DP + 0x72) != 2) return;

    /* Increment delay counter */
    uint16_t counter = bus_wram_read16(g_cpu.DP + 0x96);
    counter++;
    bus_wram_write16(g_cpu.DP + 0x96, counter);
    if (counter < 0x40) return;  /* Wait 64 frames */

    /* Store character selections from table $9293 */
    uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2E);
    if (mode == 0 || bus_wram_read16(0x0E66) != 0) {
        uint16_t sel1 = bus_wram_read16(g_cpu.DP + 0x66);
        uint16_t char1 = bus_read16(0x85, 0x9293 + sel1);
        bus_wram_write16(0x1012, char1);

        uint16_t sel2 = bus_wram_read16(g_cpu.DP + 0x68);
        uint16_t char2 = bus_read16(0x85, 0x9293 + sel2);
        bus_wram_write16(0x1112, char2);
        bus_wram_write16(0x0E62, char2);
    } else {
        if (mode != 4) {
            uint16_t sel1 = bus_wram_read16(g_cpu.DP + 0x66);
            uint16_t char1 = bus_read16(0x85, 0x9293 + sel1);
            bus_wram_write16(0x1012, char1);
        } else {
            uint16_t sel1 = bus_wram_read16(g_cpu.DP + 0x66);
            uint16_t char1 = bus_read16(0x85, 0x9293 + sel1);
            bus_wram_write16(0x1112, char1);
        }
    }

    /* Determine next transition based on game mode ($2C) */
    uint16_t game_mode = bus_wram_read16(g_cpu.DP + 0x2C);
    uint16_t next_state;
    if (game_mode == 0) {
        next_state = 0x0008;
    } else {
        next_state = 0x0016;
    }

    bus_wram_write16(g_cpu.DP + 0x32, next_state);
    bus_wram_write16(g_cpu.DP + 0x48, 0x8F00);  /* Fade out */

    printf("smk: characters selected — P1=%04X P2=%04X → transition $%04X\n",
           bus_wram_read16(0x1012), bus_wram_read16(0x1112), next_state);
}

/*
 * $85:9561 — OAM rendering for character select
 *
 * Original: sets $3C=$0300, calls $956E (sprite builder for each player),
 * then $81:CB44 (OAM staging + portrait DMA).
 *
 * Portrait = 3 × 16×16 sprites in a horizontal row (48×16 pixels).
 * Tiles $00/$02/$04 in name table 1 (VRAM $5000+), loaded by CBE4.
 * Each 16×16 sprite uses 4 tiles: N, N+1, N+16, N+17.
 *
 * CBE4 loads:
 *   $5000-$501F + $5100-$511F → sprite tile $00 (tiles $00,$01,$10,$11)
 *   $5020-$503F + $5120-$513F → sprite tile $02 (tiles $02,$03,$12,$13)
 *   $5040-$505F + $5140-$515F → sprite tile $04 (tiles $04,$05,$14,$15)
 */
static void smk_85_9561(void) {
    bus_wram_write16(g_cpu.DP + 0x3C, 0x0300);

    /* Character portraits are BG tiles at VRAM $3000 (loaded by $85:915F
     * from $7F:EA00). The tilemaps at $2400/$2800/$2C00 reference these tiles.
     * No OBJ sprites needed for portraits — they're entirely BG-based.
     *
     * TODO: implement $85:956E (OAM builder for animated character sprites)
     * and $81:CB44 (OAM staging for per-frame sprite tile DMA). These handle
     * the animated kart sprite that appears when a character is selected. */
}

/*
 * $84:F4E6 — Y button toggle (character lock/unlock)
 */
static void smk_84_F4E6(void) {
    uint16_t game_mode = bus_wram_read16(g_cpu.DP + 0x2C);
    if (game_mode == 4 || game_mode == 6) return;

    /* Check P1 Y held + A edge */
    uint16_t p1_held = bus_wram_read16(0x0020);
    if ((p1_held & 0x4000) == 0x4000) {
        uint16_t p1_edge = bus_wram_read16(g_cpu.DP + 0x6A);
        if (p1_edge & 0x0080) {
            /* Toggle P1 character lock */
            bus_wram_write16(0x0190, 0);
            goto do_toggle;
        }
    }

    /* Check P2 Y held + A edge */
    uint16_t p2_held = bus_wram_read16(0x0022);
    if ((p2_held & 0x4000) != 0x4000) return;
    uint16_t p2_edge = bus_wram_read16(g_cpu.DP + 0x6C);
    if (!(p2_edge & 0x0080)) return;
    bus_wram_write16(0x0190, 2);

do_toggle:
    {
        /* Determine which player's slot to toggle */
        uint16_t px = bus_wram_read16(g_cpu.DP + 0x82);
        if (bus_wram_read16(g_cpu.DP + 0x70 + px) != 0) return;
        if (bus_wram_read16(0x0196 + px) != 0) return;

        uint16_t slot_base = (bus_wram_read16(0x0190) == 0) ? 0x1000 : 0x1100;
        uint16_t flags = bus_wram_read16(slot_base + 0xE2);

        if (flags & 0x4000) {
            /* Unlock */
            bus_wram_write16(slot_base + 0xE2, flags & ~0x4000);
            bus_wram_write16(0x0192 + px, 0);
            bus_wram_write16(0x0196 + px, 1);
        } else {
            /* Lock */
            bus_wram_write16(slot_base + 0xE2, flags | 0x4000);
            bus_wram_write16(0x0192 + px, 1);
            bus_wram_write16(0x0196 + px, 1);
        }
    }
}

/*
 * $84:F581 — Sprite flag processing for P1/P2
 */
static void smk_84_F581(void) {
    /* Stub — processes sprite slot flags at $10E2/$11E2.
     * Not critical for basic character selection. */
}

/*
 * $84:F48D — Animation parameter update
 * Updates cursor animation parameters ($9A/$9C) based on confirm state.
 */
static void smk_84_F48D(void) {
    uint16_t p1_confirm = bus_wram_read16(g_cpu.DP + 0x70) & 3;
    uint16_t anim_counter = bus_wram_read16(g_cpu.DP + 0x98);

    if (p1_confirm == 0 && anim_counter == 5) {
        bus_wram_write16(g_cpu.DP + 0x9A, 0x0802);
    } else {
        bus_wram_write16(g_cpu.DP + 0x9A, 0x1815);
    }

    /* P2 animation */
    uint16_t e66 = bus_wram_read16(0x0E66);
    uint16_t mode = bus_wram_read16(0x002E);
    if (e66 == 0 && mode == 0) {
        /* 1P mode, no P2 cursor */
    } else {
        uint16_t p2_confirm = bus_wram_read16(g_cpu.DP + 0x72) & 3;
        if (p2_confirm == 0 && anim_counter == 5) {
            bus_wram_write16(g_cpu.DP + 0x9C, 0x0802);
        } else {
            if (e66 != 0) {
                bus_wram_write16(g_cpu.DP + 0x9C, 0x1C4C);
            } else {
                bus_wram_write16(g_cpu.DP + 0x9C, 0x1C17);
            }
        }
    }

    /* Increment animation counter, wrap at 6 */
    anim_counter++;
    if (anim_counter >= 6) anim_counter = 0;
    bus_wram_write16(g_cpu.DP + 0x98, anim_counter);
}

/*
 * $85:90B1 — Mode select per-frame logic
 *
 * Called from state $06 main loop handler.
 * Processes button input, updates cursor, handles mode selection.
 *
 * Original: PHB/PHK/PLB, 10 sub-calls, PLB/RTL
 */
void smk_8590B1(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);

    smk_85_92F9();    /* Input processing */
    /* JSR $935B — cursor update (P2 battle mode only, stub for now) */
    smk_85_9348();    /* Animation counters */
    smk_85_939F();    /* D-pad selection */
    smk_84_F4E6();    /* Y button toggle */
    smk_84_F581();    /* Sprite flag processing (stub) */
    smk_84_F48D();    /* Animation params */
    smk_85_9487();    /* Confirm/cancel */

    /* In 1P mode ($2E=0), auto-confirm P2 when P1 is confirmed */
    {
        uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2E);
        if (mode == 0) {
            uint16_t p1_state = bus_wram_read16(g_cpu.DP + 0x70);
            if (p1_state == 2) {
                bus_wram_write16(g_cpu.DP + 0x72, 2);
            }
        }
    }

    smk_85_965B();    /* Transition trigger */
    smk_85_9561();    /* OAM rendering */

    g_cpu.DB = saved_db;
}

/*
 * $85:90D7 — Mode select NMI rendering
 *
 * Called from NMI state $06 handler.
 * Handles sprite DMA, BG scroll updates, HDMA for mode select screen.
 *
 * Original: PHB/PHK/PLB, JSL $81CB35, multiple DMA/scroll pairs, PLB/RTL
 */
void smk_8590D7(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);

    /* JSL $81CB35 — sprite tile DMA */
    smk_81CB35();

    /* VRAM cursor updates for P1 and P2 */
    for (uint16_t px = 0; px <= 2; px += 2) {
        /* $96D6: Set VRAM addr to old selection position */
        uint16_t old_pos = bus_wram_read16(g_cpu.DP + 0x74 + px);
        bus_write16(0x85, 0x2116, old_pos);

        /* $96EC: Clear old cursor (write background pattern) */
        uint16_t bg_pat = bus_wram_read16(g_cpu.DP + 0x84);
        bus_write16(0x00, 0x2118, bg_pat);
        bus_write16(0x00, 0x2118, bg_pat + 1);

        /* $96DC: Set VRAM addr to new selection position */
        uint16_t new_pos = bus_wram_read16(g_cpu.DP + 0x7E + px);
        bus_write16(0x85, 0x2116, new_pos);

        /* $96E2: Draw new cursor */
        uint16_t cursor_pat = bus_wram_read16(g_cpu.DP + 0x78 + px);
        bus_write16(0x00, 0x2118, cursor_pat);
        bus_write16(0x00, 0x2118, cursor_pat + 1);
    }

    /* $96F6/$96FC: VRAM portrait tile updates for P1 */
    for (uint16_t px = 0; px <= 2; px += 2) {
        uint16_t vram_addr = bus_wram_read16(g_cpu.DP + 0x86 + px);
        bus_write16(0x85, 0x2116, vram_addr);

        /* Read from portrait table based on selection */
        uint16_t sel_idx = bus_wram_read16(0x0180 + px);
        uint16_t table_ptr = bus_read16(0x85, 0x9241 + sel_idx);
        for (int i = 0; i < 4; i++) {
            uint16_t tile = bus_read16(0x00, table_ptr + i * 2);
            bus_write16(0x00, 0x2118, tile);
        }
    }

    /* $9717/$971F: Character sprite area updates */
    for (uint16_t px = 0; px <= 2; px += 2) {
        uint16_t sprite_addr = bus_wram_read16(g_cpu.DP + 0x8C + px);
        bus_wram_write16(g_cpu.DP + 0x92, sprite_addr);
        bus_write16(0x85, 0x2116, sprite_addr);

        uint16_t draw_mode = bus_wram_read16(0x0184 + px);
        if (draw_mode == 0) {
            /* Fill character portrait area (6×4 + 6×4 tiles) */
            uint16_t addr = sprite_addr;
            for (int row = 0; row < 4; row++) {
                for (int col = 0; col < 6; col++) {
                    bus_write16(0x00, 0x2118, 0x281C);
                }
                addr += 0x20;
                bus_wram_write16(g_cpu.DP + 0x92, addr);
                bus_write16(0x85, 0x2116, addr);
            }
            for (int row = 0; row < 4; row++) {
                for (int col = 0; col < 6; col++) {
                    bus_write16(0x00, 0x2118, 0x2410);
                }
                addr += 0x20;
                bus_wram_write16(g_cpu.DP + 0x92, addr);
                bus_write16(0x85, 0x2116, addr);
            }
        }
    }

    /* $978F: Cursor animation data write */
    for (uint16_t px = 0; px <= 2; px += 2) {
        /* Check conditions for second player */
        if (px == 2) {
            uint16_t e66 = bus_wram_read16(0x0E66);
            if (e66 == 0) {
                uint16_t mode = bus_wram_read16(g_cpu.DP + 0x2E);
                if (mode == 0) continue;  /* Skip P2 in 1P mode */
            }
        }
        uint16_t anim = bus_wram_read16(g_cpu.DP + 0x9A + px);
        bus_write16(0x00, 0x2118, anim);
        bus_write16(0x00, 0x2118, anim + 1);
    }

    /* BG scroll writes */
    op_sep(0x20);
    op_lda_dp8(0x61);
    op_sta_long8(0x00, 0x210F);  /* BG1HOFS low */
    op_lda_dp8(0x62);
    op_sta_long8(0x00, 0x210F);  /* BG1HOFS high */
    op_lda_dp8(0x64);
    op_sta_long8(0x00, 0x2111);  /* BG2VOFS low */
    op_lda_dp8(0x65);
    op_sta_long8(0x00, 0x2111);  /* BG2VOFS high */
    op_rep(0x20);

    g_cpu.DB = saved_db;
}
