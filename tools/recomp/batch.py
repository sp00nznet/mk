#!/usr/bin/env python3
"""
batch.py — run autogen.py over a profile's call targets and report the yield.

Reads `PROF <addr> <count> <P> [MULTI-MX] [recompiled]` lines (from
SMK_RECOMP_PROFILE=1 stderr) and tries to auto-generate each not-yet-recompiled,
single-(M,X) target. Prints the generated functions for the successes and a
summary of why the rest were skipped — the throughput view of the auto-generator.

Usage:
  py tools/recomp/batch.py <rom.sfc> <prof.txt> [out.c]
"""
import sys, os, subprocess, collections

HERE = os.path.dirname(os.path.abspath(__file__))
AUTOGEN = os.path.join(HERE, "autogen.py")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    rom, prof = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else None

    cands, skipped = [], collections.Counter()
    for line in open(prof):
        f = line.split()
        if not f or f[0] != "PROF":
            continue
        addr, count, P = f[1], int(f[2]), f[3]
        rest = " ".join(f[4:])
        if "recompiled" in rest:
            skipped["already recompiled"] += 1
            continue
        if "MULTI-MX" in rest:
            skipped["multi-(M,X) entry"] += 1
            continue
        cands.append((addr, count, P))

    gen, fails = [], collections.Counter()
    for addr, count, P in cands:
        loc = f"{addr[:2]}:{addr[2:]}"
        name = f"smk_{addr}"
        r = subprocess.run([sys.executable, AUTOGEN, rom, loc, P, name],
                           capture_output=True, text=True)
        if r.returncode == 0:
            gen.append((addr, count, r.stdout.rstrip()))
        else:
            reason = r.stderr.strip().replace("// UNSUPPORTED: ", "")
            # bucket by the leading phrase
            key = reason.split(" at ")[0].split(" (")[0]
            fails[key] += 1

    gen.sort(key=lambda t: -t[1])
    print(f"# candidates: {len(cands)}   generated: {len(gen)}   unsupported: {len(cands) - len(gen)}",
          file=sys.stderr)
    print("# skipped before autogen:", dict(skipped), file=sys.stderr)
    print("# unsupported reasons:", file=sys.stderr)
    for k, v in fails.most_common():
        print(f"#   {v:3d}  {k}", file=sys.stderr)
    print("# generated (addr / call-count):", file=sys.stderr)
    for addr, count, _ in gen:
        print(f"#   {addr}  x{count}", file=sys.stderr)

    if out_path:
        with open(out_path, "w") as f:
            f.write("/* batch-autogen output — validate each via the diff harness. */\n")
            for _, _, body in gen:
                f.write(body + "\n\n")
        print(f"# wrote {len(gen)} functions -> {out_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
