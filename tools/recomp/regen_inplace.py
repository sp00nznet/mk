#!/usr/bin/env python3
"""
regen_inplace.py — regenerate src/recomp/smk_autogen.c from its own function
comments (each autogen header records `from $BB:AAAA` and `(P=$PP)`), re-running
autogen so emit changes (e.g. cycle-accurate recomp_tick) apply without needing
the original profile. One process; no /tmp.

Usage: py tools/recomp/regen_inplace.py <rom.sfc> <smk_autogen.c>
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import autogen

def main():
    rom, path = sys.argv[1], sys.argv[2]
    data = open(rom, "rb").read()
    if len(data) % 1024 == 512:
        data = data[512:]
    src = open(path).read()
    # pair each "from $BB:AAAA ... (P=$PP)" header with its function
    items = re.findall(r"from \$([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s*\n\s*\*\s*entry[^\n]*\(P=\$([0-9A-Fa-f]{2})\)", src)
    hdr = ('/*\n * smk_autogen.c - autogen.py output (cycle-accurate recomp_tick calls; no-op\n'
           ' * unless SMK_RECOMP_CYCLEACCURATE). Each gated byte-identical to the emulation\n'
           ' * oracle through a race. Regenerate: py tools/recomp/regen_inplace.py <rom> <this>\n */\n'
           '#include "smk/functions.h"\n#include <snesrecomp/snesrecomp.h>\n'
           '#include <snesrecomp/func_table.h>\n#include <stdint.h>\n\n'
           '/* Link anchor: forces this static-lib TU (and its registrations) to link. */\n'
           'void smk_autogen_link_anchor(void) {}\n\n')
    parts, n = [hdr], 0
    for bb, aaaa, pp in items:
        bank, a, P = int(bb, 16), int(aaaa, 16), int(pp, 16)
        name = "smk_%02X%04X" % (bank, a)
        parts.append(autogen.generate(data, bank, a, P, name) + "\n")
        n += 1
    open(path, "w").write("\n".join(parts))
    print("regenerated %d functions in %s" % (n, path))

if __name__ == "__main__":
    main()
