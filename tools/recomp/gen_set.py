#!/usr/bin/env python3
"""
gen_set.py — generate src/recomp/smk_autogen.c for a set of functions in ONE
process (imports autogen; no per-function subprocess like batch.py). Reads a
profile file's `PROF <addr> <count> <P> [flags]` lines, skips recompiled entries,
uses live-flag entry dispatch for MULTI-MX entries,
and emits each via autogen.generate(). Keeps the link anchor.

Usage: py tools/recomp/gen_set.py <rom.sfc> <prof.txt> <out.c>
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import autogen


def instrument_semantics(body, addr):
    """Observe completed direction calls, never partial interrupt redirects."""
    if addr not in ('81F638', '81F722'):
        return body
    lines = []
    for line in body.splitlines():
        if re.match(r'    return;\s*/\* \$[0-9A-F]+ RT[LS]', line):
            lines.append('    smk_semantic_end(&_semantic_check);')
        lines.append(line)
        if line.startswith('RECOMP_PATCH('):
            lines.append(f'    SmkSemanticCheck _semantic_check = smk_semantic_begin(0x{addr});')
    return '\n'.join(lines)

def main():
    rom, prof, out = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(rom, "rb").read()
    if len(data) % 1024 == 512:
        data = data[512:]
    rows = []
    for line in open(prof, encoding="utf-8", errors="replace"):
        f = line.split()
        if len(f) >= 4 and f[0] == "PROF" and "recompiled" not in line:
            variant = next((s.split("=", 1)[1] for s in f[4:]
                            if s.startswith("VARIANTS=")), None)
            P = ([int(p, 16) for p in variant.split(",")] if variant else
                 None if "MULTI-MX" in line else int(f[3], 16))
            rows.append((f[1], P))
    hdr = ('/*\n * smk_autogen.c - autogen.py output (cycle-accurate recomp_tick calls).\n'
           ' * Entry flags come from a real-ROM profile; main.c selects the oracle-gated\n'
           ' * default subset. Other generated bodies remain opt-in for focused work.\n'
           ' * Regenerate: py tools/recomp/gen_set.py <rom> <prof> <this>\n */\n'
           '#include "smk/functions.h"\n#include "smk/recomp_ops.h"\n#include "smk/semantic_verify.h"\n#include <snesrecomp/snesrecomp.h>\n'
           '#include <snesrecomp/func_table.h>\n#include <stdint.h>\n\n'
           '/* Link anchor: forces this static-lib TU (and its registrations) to link. */\n'
           'void smk_autogen_link_anchor(void) {}\n\n')
    parts, n = [hdr], 0
    for addr, P in rows:
        bank, a = int(addr[:2], 16), int(addr[2:], 16)
        try:
            body = autogen.generate(data, bank, a, P, "smk_" + addr)
            parts.append(instrument_semantics(body, addr) + "\n")
            n += 1
        except autogen.Unsupported as e:
            print("skip %s: %s" % (addr, e), file=sys.stderr)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts))
    print("wrote %d functions -> %s" % (n, out))

if __name__ == "__main__":
    main()
