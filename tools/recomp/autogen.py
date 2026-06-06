#!/usr/bin/env python3
"""
autogen.py — 65816 -> RECOMP_PATCH auto-generator (v2).

Decodes an SNES function from ROM bytes + trace-exact entry M/X flags and emits
a snesrecomp RECOMP_PATCH that reproduces it, gated byte-identical to the
emulation oracle (tools/diff_snapshots.py vs lakesnes_ref).

Inspired by SuperRecomp's per-opcode codegen, but emitting OUR runtime (g_cpu +
bus_read/write + op_* helpers) and seeded with trace-exact entry flags — the two
assets SuperRecomp/mstan lack (docs/own_recompiler_design.md).

v2 handles:
  - loads/stores/STZ across addressing modes (imm/dp/dp,x/dp,y/abs/abs,x/abs,y/
    long/long,x) via INLINE bus_read/write + N/Z flags (no per-mode C helper),
  - register/stack/flag ops via the existing op_* helpers,
  - intra-function branches (BEQ/BNE/BCS/BCC/BMI/BPL/BVS/BVC/BRA/BRL) via a CFG
    walk + C labels/gotos.
Calls (JSR/JSL), computed/indirect jumps, and per-(M,X) re-entry with differing
widths raise Unsupported -> the function falls back to a hand-port / interp.

Usage:
  py tools/recomp/autogen.py <rom.sfc> <bank:addr> <entry_P_hex> [func_name]
"""
import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "disasm"))
from disasm65816 import OPCODES, rom_read  # noqa: E402


class Unsupported(Exception):
    pass


TERMINALS = {0x60, 0x6B, 0x40, 0xDB}                       # RTS RTL RTI STP
BRANCH = {0xF0: ("Z", True), 0xD0: ("Z", False),          # BEQ BNE
          0xB0: ("C", True), 0x90: ("C", False),          # BCS BCC
          0x30: ("N", True), 0x10: ("N", False),          # BMI BPL
          0x70: ("V", True), 0x50: ("V", False)}          # BVS BVC
UNCOND = {0x80, 0x82}                                      # BRA BRL
CALL = {0x20, 0x22}                                        # JSR JSL (fall through)
TAILJMP = {0x4C, 0x5C}                                     # JMP JML (tail call -> return)
INDIRECT = {0x6C, 0x7C, 0xDC, 0xFC}                        # (abs)/(abs,x)/[abs]/(dp,x) — dynamic
MEM_MODES = {"abs", "absx", "absy", "dp", "dpx", "dpy", "long", "longx"}


def _call_target(insn, bank):
    """24-bit target of a JSR/JSL/JMP/JML (long modes carry their own bank)."""
    return insn["val"] if insn["mode"] == "long" else ((bank << 16) | insn["val"])


def _call_stmt(insn, bank):
    t = _call_target(insn, bank)
    fn = "func_table_call" if insn["op"] in (0x22, 0x5C) else "func_table_call_jsr"
    return f"{fn}(0x{t:06X});"


def _decode_at(data, bank, pc, m8, x8):
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
    total = 1 + size
    target = None
    if mode == "rel8":
        d = raw[1] if raw[1] < 128 else raw[1] - 256
        target = (pc + total + d) & 0xFFFF
    elif mode == "rel16":
        d = val if val < 0x8000 else val - 0x10000
        target = (pc + total + d) & 0xFFFF
    nm8, nx8 = m8, x8
    if op == 0xC2:  # REP
        if raw[1] & 0x20: nm8 = False
        if raw[1] & 0x10: nx8 = False
    elif op == 0xE2:  # SEP
        if raw[1] & 0x20: nm8 = True
        if raw[1] & 0x10: nx8 = True
    return dict(pc=pc, op=op, name=name, mode=mode, val=val, total=total,
                target=target, m8=m8, x8=x8), nm8, nx8


def decode_cfg(data, bank, addr, m8, x8, limit=2048):
    """Walk all reachable instructions from the entry, threading M/X. Returns
    (insns_by_pc, branch_targets)."""
    insns, targets = {}, set()
    work = [(addr, m8, x8)]
    n = 0
    while work:
        pc, m, x = work.pop()
        if pc in insns:
            if (insns[pc]["m8"], insns[pc]["x8"]) != (m, x):
                raise Unsupported(f"${pc:04X} re-entered with differing M/X "
                                  f"(per-(M,X) variants not handled in v2)")
            continue
        n += 1
        if n > limit:
            raise Unsupported("function too large / unbounded")
        ins, nm, nx = _decode_at(data, bank, pc, m, x)
        insns[pc] = ins
        op = ins["op"]
        if op in TERMINALS or op in TAILJMP:
            continue                                   # no in-function successor
        if op in INDIRECT:
            raise Unsupported(f"indirect {ins['name']} (${op:02X}) at ${pc:04X} — dynamic target")
        nxt = (pc + ins["total"]) & 0xFFFF
        if op in UNCOND:
            targets.add(ins["target"]); work.append((ins["target"], nm, nx))
        elif op in BRANCH:
            targets.add(ins["target"])
            work.append((ins["target"], nm, nx)); work.append((nxt, nm, nx))
        else:
            # JSR/JSL fall through (we assume the callee preserves M/X — the diff
            # gate rejects any function where that doesn't hold).
            work.append((nxt, nm, nx))
    return insns, targets


# ---- inline emit helpers -------------------------------------------------
def _ea(mode, val):
    """(bank_expr, addr_expr) for a memory operand."""
    if mode == "abs":   return ("g_cpu.DB", f"0x{val:04X}")
    if mode == "absx":  return ("g_cpu.DB", f"(uint16_t)(0x{val:04X} + g_cpu.X)")
    if mode == "absy":  return ("g_cpu.DB", f"(uint16_t)(0x{val:04X} + g_cpu.Y)")
    if mode == "dp":    return ("0x00", f"(uint16_t)(g_cpu.DP + 0x{val:02X})")
    if mode == "dpx":   return ("0x00", f"(uint16_t)(g_cpu.DP + 0x{val:02X} + g_cpu.X)")
    if mode == "dpy":   return ("0x00", f"(uint16_t)(g_cpu.DP + 0x{val:02X} + g_cpu.Y)")
    if mode == "long":  return (f"0x{(val >> 16) & 0xFF:02X}", f"0x{val & 0xFFFF:04X}")
    if mode == "longx": return (f"0x{(val >> 16) & 0xFF:02X}", f"(uint16_t)(0x{val & 0xFFFF:04X} + g_cpu.X)")
    raise Unsupported(f"addr mode {mode}")


def _wide(insn, kind):  # kind 'm' (accumulator) or 'x' (index)
    return (not insn["m8"]) if kind == "m" else (not insn["x8"])


def _reg_write(reg, w, v):
    if reg == "A":
        return (f"g_cpu.C = (uint16_t)(({v}) & 0xFFFF);" if w == 16
                else f"g_cpu.C = (uint16_t)((g_cpu.C & 0xFF00) | (({v}) & 0xFF));")
    field = "g_cpu." + reg
    return (f"{field} = (uint16_t)(({v}) & 0xFFFF);" if w == 16
            else f"{field} = (uint16_t)(({v}) & 0xFF);")


def _reg_read(reg, w):
    field = "g_cpu.C" if reg == "A" else "g_cpu." + reg
    return field if w == 16 else f"(uint8_t)({field} & 0xFF)"


def _nz(w, v):
    bit = 15 if w == 16 else 7
    return (f"g_cpu.flag_N = (uint8_t)(((uint{w}_t)({v}) >> {bit}) & 1); "
            f"g_cpu.flag_Z = (uint8_t)((uint{w}_t)({v}) == 0);")


def _src(insn, w):
    """C expression for a read operand (immediate or memory)."""
    mode, val = insn["mode"], insn["val"]
    if mode in ("imm8", "immA", "immX"):
        return f"0x{val:0{w // 4}X}"
    if mode in MEM_MODES:
        bank, addr = _ea(mode, val)
        return f"bus_read{w}({bank}, {addr})"
    raise Unsupported(f"{insn['name']} {mode}")


def _load(insn, reg, kind):
    w = 16 if _wide(insn, kind) else 8
    return f"{{ uint{w}_t _v = (uint{w}_t)({_src(insn, w)}); {_reg_write(reg, w, '_v')} {_nz(w, '_v')} }}"


def _logical(insn, c_op):  # AND/ORA/EOR: A = A <op> src; N/Z
    w = 16 if _wide(insn, "m") else 8
    return (f"{{ uint{w}_t _a = (uint{w}_t)({_reg_read('A', w)}); "
            f"uint{w}_t _v = (uint{w}_t)({_src(insn, w)}); "
            f"_a = (uint{w}_t)(_a {c_op} _v); {_reg_write('A', w, '_a')} {_nz(w, '_a')} }}")


def _cmp(insn, reg, kind):  # CMP/CPX/CPY: flags from reg - src
    w = 16 if _wide(insn, kind) else 8
    return (f"{{ uint{w}_t _a = (uint{w}_t)({_reg_read(reg, w)}); "
            f"uint{w}_t _v = (uint{w}_t)({_src(insn, w)}); uint{w}_t _t = (uint{w}_t)(_a - _v); "
            f"g_cpu.flag_C = (uint8_t)(_a >= _v); g_cpu.flag_Z = (uint8_t)(_a == _v); "
            f"g_cpu.flag_N = (uint8_t)((_t >> {w - 1}) & 1); }}")


def _incdec(insn, reg, delta):  # INX/INY/DEX/DEY (X width)
    w = 16 if _wide(insn, "x") else 8
    sign = "+" if delta > 0 else "-"
    return (f"{{ uint{w}_t _t = (uint{w}_t)(({_reg_read(reg, w)}) {sign} 1); "
            f"{_reg_write(reg, w, '_t')} {_nz(w, '_t')} }}")


def _store(insn, reg, kind):
    w = 16 if _wide(insn, kind) else 8
    mode, val = insn["mode"], insn["val"]
    if mode not in MEM_MODES:
        raise Unsupported(f"{insn['name']} {mode}")
    bank, addr = _ea(mode, val)
    v = "0" if reg is None else _reg_read(reg, w)
    return f"bus_write{w}({bank}, {addr}, (uint{w}_t)({v}));"


# register/stack/flag ops that already have an op_* helper
_SIMPLE = {
    "XBA": "op_xba();", "XCE": "op_xce();",
    "TAX": "op_tax();", "TAY": "op_tay();", "TXA": "op_txa();", "TYA": "op_tya();",
    "PHA": "op_pha16();", "PLA": "op_pla16();", "PHP": "op_php();", "PLP": "op_plp();",
    "PHX": "op_phx16();", "PLX": "op_plx16();", "PHY": "op_phy16();", "PLY": "op_ply16();",
    "PHB": "op_phb();", "PLB": "op_plb();",
}
_LOADS = {"LDA": ("A", "m"), "LDX": ("X", "x"), "LDY": ("Y", "x")}
_STORES = {"STA": ("A", "m"), "STX": ("X", "x"), "STY": ("Y", "x"), "STZ": (None, "m")}
_LOGIC = {"AND": "&", "ORA": "|", "EOR": "^"}
_INCDEC = {"INX": ("X", +1), "INY": ("Y", +1), "DEX": ("X", -1), "DEY": ("Y", -1)}


def emit_body(insn):
    op, name = insn["op"], insn["name"]
    if name in ("REP", "SEP"):
        return f"op_{name.lower()}(0x{insn['val']:02X});"
    if name in _SIMPLE:
        return _SIMPLE[name]
    if name in _LOADS:
        return _load(insn, *_LOADS[name])
    if name in _STORES:
        return _store(insn, *_STORES[name])
    if name in _LOGIC:
        return _logical(insn, _LOGIC[name])
    if name == "CMP":
        return _cmp(insn, "A", "m")
    if name == "CPX":
        return _cmp(insn, "X", "x")
    if name == "CPY":
        return _cmp(insn, "Y", "x")
    if name in _INCDEC:
        return _incdec(insn, *_INCDEC[name])
    if name == "INC" and insn["mode"] == "dp":
        return f"op_inc_dp{16 if _wide(insn, 'm') else 8}(0x{insn['val']:02X});"
    if name in ("ADC", "SBC"):
        if insn["mode"] == "immA" and not insn["m8"]:
            return f"op_{name.lower()}_imm16(0x{insn['val']:04X});"
        raise Unsupported(f"{name} {insn['mode']} (only imm16 has a helper)")
    raise Unsupported(f"no emit rule for {name} {insn['mode']} (${op:02X}) at ${insn['pc']:04X}")


def generate(data, bank, addr, P, name):
    m8, x8 = bool(P & 0x20), bool(P & 0x10)
    insns, targets = decode_cfg(data, bank, addr, m8, x8)
    out = []
    pc24 = (bank << 16) | addr
    out.append(f"/* Auto-generated by tools/recomp/autogen.py from ${bank:02X}:{addr:04X}")
    out.append(f" * entry M={int(m8)} X={int(x8)} (P=${P:02X}). Validate with the diff harness. */")
    out.append(f"RECOMP_PATCH({name}, 0x{pc24:06X}) {{")
    for pc in sorted(insns):
        ins = insns[pc]
        if pc in targets:
            out.append(f"  L_{pc:04X}:;")
        op = ins["op"]
        if op in TERMINALS:
            out.append(f"    return;            /* ${pc:04X} {ins['name']} */")
        elif op in CALL:
            out.append(f"    {_call_stmt(ins, bank):<46s} /* ${pc:04X} {ins['name']} ${_call_target(ins, bank):06X} */")
        elif op in TAILJMP:
            out.append(f"    {_call_stmt(ins, bank)} return;  /* ${pc:04X} {ins['name']} (tail) ${_call_target(ins, bank):06X} */")
        elif op in UNCOND:
            out.append(f"    goto L_{ins['target']:04X};   /* ${pc:04X} {ins['name']} */")
        elif op in BRANCH:
            flag, want = BRANCH[op]
            cond = f"g_cpu.flag_{flag}" if want else f"!g_cpu.flag_{flag}"
            out.append(f"    if ({cond}) goto L_{ins['target']:04X};  /* ${pc:04X} {ins['name']} */")
        else:
            out.append(f"    {emit_body(ins):<46s} /* ${pc:04X} {ins['name']} */")
    out.append("}")
    return "\n".join(out)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    rom_path, loc, p_hex = sys.argv[1], sys.argv[2], sys.argv[3]
    bank, addr = (int(v, 16) for v in loc.split(":"))
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
