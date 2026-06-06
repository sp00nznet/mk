#!/usr/bin/env python3
"""
gen_set.py — generate src/recomp/smk_autogen.c for a set of functions in ONE
process (imports autogen; no per-function subprocess like batch.py). Reads a
profile file's `PROF <addr> <count> <P> [flags]` lines, skips recompiled/MULTI-MX,
and emits each via autogen.generate(). Keeps the link anchor.

Usage: py tools/recomp/gen_set.py <rom.sfc> <prof.txt> <out.c>
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import autogen

def main():
    rom, prof, out = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(rom, "rb").read()
    if len(data) % 1024 == 512:
        data = data[512:]
    rows = []
    for line in open(prof):
        f = line.split()
        if len(f) >= 4 and f[0] == "PROF" and "recompiled" not in line and "MULTI-MX" not in line:
            rows.append((f[1], int(f[3], 16)))
    hdr = ('/*\n * smk_autogen.c - autogen.py output (cycle-accurate recomp_tick calls; no-op\n'
           ' * unless SMK_RECOMP_CYCLEACCURATE). Each gated byte-identical to the emulation\n'
           ' * oracle through a race. Regenerate: py tools/recomp/gen_set.py <rom> <prof> <this>\n */\n'
           '#include "smk/functions.h"\n#include <snesrecomp/snesrecomp.h>\n'
           '#include <snesrecomp/func_table.h>\n#include <stdint.h>\n\n'
           '/* Link anchor: forces this static-lib TU (and its registrations) to link. */\n'
           'void smk_autogen_link_anchor(void) {}\n\n')
    parts, n = [hdr], 0
    for addr, P in rows:
        bank, a = int(addr[:2], 16), int(addr[2:], 16)
        try:
            parts.append(autogen.generate(data, bank, a, P, "smk_" + addr) + "\n")
            n += 1
        except autogen.Unsupported as e:
            print("skip %s: %s" % (addr, e), file=sys.stderr)
    open(out, "w").write("\n".join(parts))
    print("wrote %d functions -> %s" % (n, out))

if __name__ == "__main__":
    main()
