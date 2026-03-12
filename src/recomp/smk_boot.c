/*
 * Super Mario Kart — Boot chain recompilation
 *
 * Reset vector → hardware init → main loop
 * These are the first functions that run when the game starts.
 */

#include "smk/functions.h"
#include "smk/cpu_ops.h"
#include <snesrecomp/snesrecomp.h>
#include <stdio.h>

/*
 * $80:FF70 — Reset vector
 *
 * Original:
 *   SEI
 *   REP #$09       ; clear carry + decimal
 *   XCE            ; native mode (carry was cleared)
 *   SEP #$30       ; 8-bit A, 8-bit X/Y
 *   LDA #$1F
 *   XBA            ; B = $1F
 *   LDA #$FF
 *   TCS            ; S = $1FFF
 *   LDA #$01
 *   STA $420D      ; FastROM enable
 *   JML $80803A
 */
void smk_80FF70(void) {
    OP_SEI();
    op_rep(0x09);       /* clear carry + decimal */
    op_xce();           /* native mode */
    op_sep(0x30);       /* M=1, X=1 (8-bit A and index) */

    op_lda_imm8(0x1F);
    op_xba();           /* B = $1F, A = garbage */
    op_lda_imm8(0xFF);
    OP_TCS();           /* S = $1FFF */

    op_lda_imm8(0x01);
    op_sta_abs8(0x420D); /* MEMSEL = 1 (fast ROM) */

    /* JML $80803A — fall through to hardware init */
    g_cpu.PB = 0x80;
    smk_80803A();
}

/*
 * $80:803A — Hardware init entry point
 *
 * Sets DB=$80, disables interrupts/DMA, force-blanks the screen,
 * then calls the full init subroutine at $81:E000.
 * After init, enters the main loop.
 */
void smk_80803A(void) {
    /* PHK / PLB — DB = $80 */
    OP_SET_DB(0x80);

    /* STZ $4200 — disable NMI/IRQ */
    op_stz_abs8(0x4200);
    /* STZ $420B — disable DMA */
    op_stz_abs8(0x420B);
    /* STZ $420C — disable HDMA */
    op_stz_abs8(0x420C);

    /* LDA #$8F / STA $2100 — force blank + max brightness */
    op_lda_imm8(0x8F);
    op_sta_abs8(0x2100);

    /* STZ $4016 — joypad strobe off */
    op_stz_abs8(0x4016);
    /* STA $4201 — WRIO = $8F */
    op_sta_abs8(0x4201);

    /* JSL $81E000 — full initialization */
    smk_81E000();

    printf("smk: boot chain complete, entering main loop\n");

    /* The main loop is now driven by main.c's frame loop,
     * not by the original spin-wait here. */
}

/*
 * $80:8056 — Main loop (one iteration)
 *
 * Original spins on DP $44 waiting for NMI. In the recomp,
 * this is called once per frame from main.c after NMI processing.
 *
 * Original:
 *   REP #$30
 *   JSL $81E067     ; frame setup / transition
 *   STZ $44         ; clear NMI flag
 *   <wait for NMI>  ; (handled by frame loop in recomp)
 *   LDX $36         ; game state index
 *   JSR ($8197,x)   ; call state handler
 */
void smk_808056(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x80);

    op_rep(0x30);       /* 16-bit A and X/Y */

    /* JSL $81E067 — frame setup */
    smk_81E067();

    /* STZ $44 — clear NMI flag (in original, waits for NMI to set it) */
    op_stz_dp16(0x44);

    /* In the recomp, NMI has already been processed for this frame.
     * DP $44 has been set by the NMI handler. */

    /* LDX $36 — load game state index */
    op_ldx_dp16(0x36);

    /* JSR ($8197,x) — call state handler via indirect table */
    {
        uint16_t table_addr = 0x8197;
        uint16_t handler = bus_read16(0x80, table_addr + g_cpu.X);
        /* Call the handler if it's registered */
        uint32_t full_addr = 0x800000 | handler;
        func_table_call(full_addr);
    }

    g_cpu.DB = saved_db;
}

/*
 * $80:8000 — NMI handler
 *
 * Saves registers, reads RDNMI to acknowledge, increments frame
 * counter, runs brightness/sync handler, dispatches NMI state
 * handler, restores registers.
 */
void smk_808000(void) {
    /* PHP / REP #$38 / PHB / PHK / PLB / PHA / PHX / PHY */
    op_php();
    op_rep(0x38);       /* 16-bit A/X/Y, clear decimal+carry */
    op_phb();
    OP_SET_DB(0x80);    /* PHK;PLB */
    op_pha16();
    op_phx16();
    op_phy16();

    /* LDA $4210 — read RDNMI (clears NMI flag in hardware) */
    op_lda_abs16(0x4210);

    /* INC $34 — increment frame counter */
    op_inc_dp16(0x34);

    /* JSR $B181 — brightness / frame sync handler */
    smk_80B181();

    /* LDA $000036 / TAX / JSR ($81BF,x) — NMI state dispatch */
    {
        uint16_t state = bus_read16(0x00, 0x0036);
        g_cpu.X = state;
        uint16_t table_addr = 0x81BF;
        uint16_t handler = bus_read16(0x80, table_addr + g_cpu.X);
        uint32_t full_addr = 0x800000 | handler;
        func_table_call(full_addr);
    }

    /* PLY / PLX / PLA / PLB / PLP */
    op_ply16();
    op_plx16();
    op_pla16();
    op_plb();
    op_plp();
}

/*
 * $80:8067 — Main loop state handler for state $02 (initial game state handler body)
 *
 * This is one of the state handlers called from JSR ($8197,x).
 * State $02 ($36=$02) is the normal "game running" NMI-acknowledged handler.
 */
void smk_808067(void) {
    /* LDA $0162 / BMI -> skip if negative */
    op_lda_abs16(0x0162);
    if (g_cpu.flag_N) return; /* BMI $8097 -> RTS */

    /* INC $38 */
    op_inc_dp16(0x38);

    /* The rest calls many game logic subroutines.
     * For now, return — game logic will be added incrementally. */
}

/*
 * $80:80BA — State handler for state $04 (title screen main loop)
 *
 * Original:
 *   LDA $0162 / BMI -> RTS
 *   INC $38
 *   STA $015E (stores incremented $38)
 *   ... calls various title screen logic subroutines
 *
 * For now, implements the frame counter increment and basic flow.
 */
void smk_8080BA(void) {
    /* LDA $0162 / BMI -> skip if negative */
    op_lda_abs16(0x0162);
    if (g_cpu.flag_N) return;

    /* INC $38 — game frame counter */
    op_inc_dp16(0x38);

    /* LDA #$0040 / STA $015E — fade step */
    op_lda_imm16(0x0040);
    op_sta_abs16(0x015E);

    /* JSL $858045 — per-frame sprite update */
    smk_858045();

    /* JSR $853D — additional title screen logic */
    /* Skip for now */
}

/*
 * $80:8096 — Null state handler (just RTS)
 * State $00 and $1A both point here.
 */
void smk_808096(void) {
    /* RTS — does nothing */
}

/*
 * $80:81DD — NMI state handler for state $00/$1A (minimal NMI)
 */
void smk_8081DD(void) {
    /* Sets DP $44 = 1 to wake main loop from NMI wait */
    bus_wram_write16(g_cpu.DP + 0x44, 1);
}

/*
 * $80:8237 — NMI state handler for state $04 (title screen NMI)
 *
 * Original:
 *   REP #$30       (already 16-bit from NMI handler)
 *   LDA $000044    ; check NMI flag
 *   BNE $824C      ; if already set, skip (RTS)
 *   DEC A          ; A = $FFFF
 *   STA $000044    ; set NMI flag
 *   JSR $946E      ; DMA/rendering subroutine
 *   JSL $85809B    ; more rendering
 *   JSR $81B5      ; cleanup
 *   RTS
 */
void smk_808237(void) {
    op_rep(0x30);

    /* LDA $000044 */
    op_lda_long16(0x00, 0x0044);

    /* BNE -> RTS (if NMI flag already set, skip) */
    if (!g_cpu.flag_Z) return;

    /* DEC A — A = $FFFF */
    op_dec_a16();

    /* STA $000044 — set NMI flag */
    op_sta_long16(0x00, 0x0044);

    /* JSR $946E — OAM DMA transfer */
    smk_80946E();

    /* JSL $85809B — BG scroll + HDMA */
    smk_85809B();

    /* JSR $81B5 — audio/input/misc cleanup */
    smk_8081B5();
}

/*
 * $80:B181 — Brightness / fade handler
 *
 * Called from NMI. Manages screen brightness fading:
 *   DP $48 > 0: fading in (add $015E each frame, cap at $0F00)
 *   DP $48 < 0: fading out (subtract $015E each frame, cap at $8000)
 *   DP $48 = 0: no change
 * Result stored to $0160 (16-bit). High byte ($0161) written to INIDISP ($2100).
 */
void smk_80B181(void) {
    op_rep(0x30);

    /* LDA $48 */
    int16_t brightness = (int16_t)bus_wram_read16(g_cpu.DP + 0x48);

    if (brightness == 0) {
        /* No fade — just write current INIDISP */
    } else if (brightness > 0) {
        /* Fading in: add $015E */
        uint16_t step = bus_wram_read16(g_cpu.DP + 0x015E);
        uint16_t result = (uint16_t)brightness + step;
        if (result >= 0x0F00) {
            /* Fully bright */
            bus_wram_write16(g_cpu.DP + 0x48, 0);
            result = 0x0F00;
        } else {
            bus_wram_write16(g_cpu.DP + 0x48, result);
        }
        bus_wram_write16(0x0160, result);
    } else {
        /* Fading out: subtract $015E */
        uint16_t step = bus_wram_read16(g_cpu.DP + 0x015E);
        uint16_t uval = (uint16_t)brightness;
        uint16_t result = uval - step;
        if ((result & 0x8000) == 0) {
            /* Underflowed past $8000 */
            bus_wram_write16(g_cpu.DP + 0x48, 0);
            result = 0x8000;
        } else {
            bus_wram_write16(g_cpu.DP + 0x48, result);
            result &= 0x7FFF;
        }
        bus_wram_write16(0x0160, result);
    }

    /* SEP #$20 / LDA $0161 / STA $2100 */
    op_sep(0x20);
    uint8_t inidisp = bus_wram_read8(0x0161);
    bus_write8(g_cpu.DB, 0x2100, inidisp);
    op_rep(0x20);

}

/*
 * $80:946E — OAM DMA transfer
 *
 * Transfers OAM (sprite) data from WRAM $0002:0200 to PPU OAM registers
 * via DMA channel 0.
 *
 * Original:
 *   SEP #$30
 *   STZ $4302 / LDA #$02 / STA $4303   ; source = $00:0200
 *   STZ $4304                           ; source bank = $00
 *   STZ $2102 / STZ $2103               ; OAM address = 0
 *   STZ $4300                           ; DMA ctrl = 0 (1-byte, A→B)
 *   LDA #$04 / STA $4301                ; B-bus dest = $2104 (OAMDATA)
 *   LDA #$20 / STA $4305                ; size low = $20
 *   LDA #$02 / STA $4306                ; size high = $02 → $0220 bytes
 *   LDA #$01 / STA $420B                ; trigger DMA ch0
 *   REP #$30 / RTS
 */
void smk_80946E(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x80);

    op_sep(0x30);

    /* DMA source = $00:0200 */
    op_stz_abs8(0x4302);        /* src low = $00 */
    op_lda_imm8(0x02);
    op_sta_abs8(0x4303);        /* src high = $02 */
    op_stz_abs8(0x4304);        /* src bank = $00 */

    /* OAM address = 0 */
    op_stz_abs8(0x2102);
    op_stz_abs8(0x2103);

    /* DMA channel 0 setup */
    op_stz_abs8(0x4300);        /* ctrl = $00 (1-byte transfer, A→B) */
    op_lda_imm8(0x04);
    op_sta_abs8(0x4301);        /* B-bus dest = $2104 (OAMDATA) */
    op_lda_imm8(0x20);
    op_sta_abs8(0x4305);        /* size low = $20 */
    op_lda_imm8(0x02);
    op_sta_abs8(0x4306);        /* size high = $02 → total $0220 */
    op_lda_imm8(0x01);
    op_sta_abs8(0x420B);        /* trigger DMA channel 0 */

    op_rep(0x30);
    g_cpu.DB = saved_db;
}

/*
 * $85:809B — BG scroll write + HDMA trigger
 *
 * Original:
 *   PHB / PHK / PLB   ; DB = $85
 *   SEP #$30
 *   LDA $64 / STA $00210F   ; BG1 horizontal scroll (write twice)
 *   LDA $65 / STA $00210F
 *   LDA #$02 / STA $420C    ; enable HDMA channel 1
 *   REP #$30
 *   JSL $81CB35              ; additional HDMA/scroll setup
 *   PLB / RTL
 */
void smk_85809B(void) {
    uint8_t saved_db = g_cpu.DB;
    OP_SET_DB(0x85);

    op_sep(0x30);

    /* Write BG1HOFS ($210F) — two writes (low then high byte) */
    op_lda_dp8(0x64);
    op_sta_long8(0x00, 0x210F);
    op_lda_dp8(0x65);
    op_sta_long8(0x00, 0x210F);

    /* Enable HDMA channel 1 */
    op_lda_imm8(0x02);
    op_sta_abs8(0x420C);

    op_rep(0x30);

    /* JSL $81CB35 — additional scroll/HDMA setup */
    func_table_call(0x81CB35);  /* not yet recompiled, will silently fail */

    g_cpu.DB = saved_db;
}

/*
 * $80:81B5 — NMI cleanup: audio update + controller read + misc
 *
 * Original:
 *   JSR $9632    ; APU port update (writes to $2140-$2143)
 *   JSR $843C    ; controller/input processing
 *   JSR $9EB2    ; misc (mode 7 related?)
 *   RTS
 *
 * For now, stubs the sub-calls — these handle audio and input which
 * LakeSnes processes internally.
 */
void smk_8081B5(void) {
    /* JSR $9632 — APU port update
     * Writes DP $42/$43 to APU ports $2142/$2143, handles SPC700 transfers.
     * LakeSnes APU runs internally, so we write the ports to keep state in sync. */
    {
        uint8_t saved_db = g_cpu.DB;
        OP_SET_DB(0x80);
        op_sep(0x30);

        uint8_t val42 = bus_wram_read8(g_cpu.DP + 0x42);
        bus_write8(0x80, 0x2142, val42);
        uint8_t val43 = bus_wram_read8(g_cpu.DP + 0x43);
        bus_write8(0x80, 0x2143, val43);

        op_rep(0x30);
        g_cpu.DB = saved_db;
    }

    /* JSR $843C — controller/input (handled by snesrecomp input layer) */
    /* JSR $9EB2 — misc processing (stub) */
}
