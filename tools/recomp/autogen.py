#!/usr/bin/env python3
"""
autogen.py — minimal 65816 -> RECOMP_PATCH auto-generator (v1).

Decodes a straight-line SNES function and emits a snesrecomp RECOMP_PATCH body
that calls the existing op_* helpers. Seeded with the EXACT entry M/X flags
(from the LakeSnes range-tracer: `SMK_TRACE_RANGE` prints `p=`), and every
output is gated by the diff harness (tools/diff_snapshots.py vs lakesnes_ref).

Inspired by SuperRecomp's per-opcode->helper-call codegen, but emitting OUR
op_* runtime (not its helper set or mstan's CpuState ABI) and using trace-exact
entry flags — the two assets SuperRecomp/mstan lack (see
docs/own_recompiler_design.md).

v1 scope: leaf functions whose every instruction maps to an existing op_*
helper (no branches/calls/indirect, no addressing mode without a helper).
Anything unmapped raises Unsupported, so the function cleanly falls back to a
hand-port / the interpreter instead of emitting wrong code.

Usage:
  py tools/recomp/autogen.py <rom.sfc> <bank:addr> <entry_P_hex> [func_name]
  e.g. py tools/recomp/autogen.py "rom.sfc" 85:84D1 C2 smk_8584D1
       (entry_P comes from the tracer; only its M=bit5 / X=bit4 are used)
"""
import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "disasm"))
from disasm65816 import OPCODES, rom_read  # noqa: E402


class Unsupported(Exception):
    pass


# Opcodes that end a function (no body emit; the intercept hook does the return).
TERMINALS = {0x60, 0x6B, 0x40, 0xDB}          # RTS, RTL, RTI, STP
# Control-flow we don't reconstruct in v1 (branches / calls / jumps / indirect).
CTRL = {0x20, 0x22, 0x4C, 0x5C, 0x6C, 0x7C, 0xDC, 0xFC,
        0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0, 0x80, 0x82}


def decode_func(data, bank, addr, m8, x8, max_insns=512):
    """Walk a function entry->terminator, threading M/X. Returns a list of
    structured instruction dicts (the last is the terminator)."""
    insns, pc = [], addr
    for _ in range(max_insns):
        op = rom_read(data, bank, pc)
        if op not in OPCODES:
            raise Unsupported(f"unknown opcode ${op:02X} at ${bank:02X}:{pc:04X}")
        name, size, mode = OPCODES[op]
        if mode == "immA":
            size = 1 if m8 else 2
        elif mode == "immX":
            size = 1 if x8 else 2
        raw = [rom_read(data, bank, pc + i) for i in range(1 + size)]
        val = 0
        if size == 1:
            val = raw[1]
        elif size == 2:
            val = raw[1] | (raw[2] << 8)
        elif size == 3:
            val = raw[1] | (raw[2] << 8) | (raw[3] << 16)
        insns.append(dict(pc=pc, op=op, name=name, mode=mode, val=val, m8=m8, x8=x8))
        if op == 0xC2:  # REP
            if raw[1] & 0x20: m8 = False
            if raw[1] & 0x10: x8 = False
        elif op == 0xE2:  # SEP
            if raw[1] & 0x20: m8 = True
            if raw[1] & 0x10: x8 = True
        pc += 1 + size
        if op in TERMINALS:
            return insns
        if op in CTRL:
            raise Unsupported(f"control flow {name} (${op:02X}) at ${bank:02X}:{insns[-1]['pc']:04X} "
                              f"— v1 handles straight-line leaves only")
    raise Unsupported("no terminator within max_insns")


# Register-only / fixed ops.
_SIMPLE = {
    "XBA": "op_xba();", "XCE": "op_xce();",
    "TAX": "op_tax();", "TAY": "op_tay();", "TXA": "op_txa();", "TYA": "op_tya();",
    "PHA": "op_pha16();", "PLA": "op_pla16();", "PHP": "op_php();", "PLP": "op_plp();",
    "PHX": "op_phx16();", "PLX": "op_plx16();", "PHY": "op_phy16();", "PLY": "op_ply16();",
    "PHB": "op_phb();", "PLB": "op_plb();",
}

# (name, mode) -> (helper_base, operand_kind, width_flag)
#   operand_kind: 'imm' | 'addr' | 'dp' | 'long'
#   width_flag:   'm' (accumulator), 'x' (index), or None (fixed-width helper)
# Only entries whose op_* helper actually exists are listed; everything else
# raises Unsupported (-> hand-port/interp).
_MAP = {
    ("LDA", "immA"): ("op_lda_imm", "imm", "m"),
    ("LDA", "dp"):   ("op_lda_dp",  "dp",  "m"),
    ("LDA", "abs"):  ("op_lda_abs", "addr", "m"),
    ("LDA", "long"): ("op_lda_long16", "long", None),
    ("STA", "dp"):   ("op_sta_dp",  "dp",  "m"),
    ("STA", "abs"):  ("op_sta_abs", "addr", "m"),
    ("STA", "long"): ("op_sta_long", "long", "m"),
    ("STZ", "dp"):   ("op_stz_dp",  "dp",  "m"),
    ("STZ", "abs"):  ("op_stz_abs", "addr", "m"),
    ("INC", "dp"):   ("op_inc_dp",  "dp",  "m"),
    ("CMP", "immA"): ("op_cmp_imm", "imm", "m"),
    ("AND", "immA"): ("op_and_imm", "imm", "m"),
    ("ADC", "immA"): ("op_adc_imm16", "imm", None),
    ("SBC", "immA"): ("op_sbc_imm16", "imm", None),
    ("LDX", "immX"): ("op_ldx_imm", "imm", "x"),
    ("LDX", "dp"):   ("op_ldx_dp16", "dp", None),
    ("LDY", "immX"): ("op_ldy_imm16", "imm", None),
    ("STX", "abs"):  ("op_stx_abs16", "addr", None),
}


def emit_insn(insn):
    op, name, mode, val = insn["op"], insn["name"], insn["mode"], insn["val"]
    if op in TERMINALS:
        return None  # return handled by the intercept hook
    if name in ("REP", "SEP"):
        return f"op_{name.lower()}(0x{val:02X});"
    if name in _SIMPLE:
        return _SIMPLE[name]
    key = (name, mode)
    if key not in _MAP:
        raise Unsupported(f"no op_* helper for {name} {mode} (${op:02X}) at ${insn['pc']:04X}")
    base, kind, wflag = _MAP[key]
    if wflag is None:
        helper = base
    else:
        wide = (not insn["m8"]) if wflag == "m" else (not insn["x8"])
        helper = base + ("16" if wide else "8")
    if kind == "imm":
        return f"{helper}(0x{val:02X});"
    if kind == "dp":
        return f"{helper}(0x{val:02X});"
    if kind == "addr":
        return f"{helper}(0x{val:04X});"
    if kind == "long":
        bank, addr = (val >> 16) & 0xFF, val & 0xFFFF
        return f"{helper}(0x{bank:02X}, 0x{addr:04X});"
    raise Unsupported(f"operand kind {kind}")


def generate(data, bank, addr, P, name):
    m8 = bool(P & 0x20)
    x8 = bool(P & 0x10)
    insns = decode_func(data, bank, addr, m8, x8)
    body = []
    for ins in insns:
        line = emit_insn(ins)
        if line is not None:
            body.append(f"    {line:<28s} /* ${ins['pc']:04X} {ins['name']} */")
    pc24 = (bank << 16) | addr
    out = []
    out.append(f"/* Auto-generated by tools/recomp/autogen.py from ${bank:02X}:{addr:04X}")
    out.append(f" * entry M={int(m8)} X={int(x8)} (P=${P:02X}). Validate with the diff harness. */")
    out.append(f"RECOMP_PATCH({name}, 0x{pc24:06X}) {{")
    out.extend(body)
    out.append("}")
    return "\n".join(out)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    rom_path, loc, p_hex = sys.argv[1], sys.argv[2], sys.argv[3]
    bank, addr = (int(x, 16) for x in loc.split(":"))
    P = int(p_hex, 16)
    name = sys.argv[4] if len(sys.argv) > 4 else f"smk_{bank:02X}{addr:04X}"
    data = open(rom_path, "rb").read()
    if len(data) % 1024 == 512:
        data = data[512:]
    try:
        print(generate(data, bank, addr, P, name))
    except Unsupported as e:
        print(f"// UNSUPPORTED: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
