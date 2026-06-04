#!/usr/bin/env python3
"""
diff_snapshots.py — lockstep WRAM/VRAM divergence finder for SMK recomp.

Inspired by the snesrev zelda3 verification harness (zelda_cpu_infra.c), which
runs the original ROM and a reimplementation side by side and compares RAM each
frame. Here we compare two snapshot streams produced by smk_launcher's
SMK_SNAPSHOT_PREFIX dumps:

  * reference run : pure interpreter (real ROM = ground truth)
        SMK_HEADLESS=1 SMK_MAX_FRAMES=N SMK_SNAPSHOT_PREFIX=snap/ref ./smk_launcher rom.sfc
  * test run      : recompiled functions
        SMK_INTERP=0 SMK_HEADLESS=1 SMK_MAX_FRAMES=N SMK_SNAPSHOT_PREFIX=snap/recomp ./smk_launcher rom.sfc

Then:
  py tools/diff_snapshots.py snap/ref snap/recomp

It reports, per frame, the FIRST WRAM byte and FIRST VRAM word that differ —
i.e. the earliest effect of the first incorrect recompiled function — plus a
summary of all diverging regions on the first bad frame.

Snapshot format (see snesrecomp_dump_snapshot):
  char magic[8]="SMKSNAP1"; u32 wram_size; u32 vram_size; u8 wram[]; u8 vram[]
"""

import argparse
import glob
import os
import re
import struct
import sys

MAGIC_V1 = b"SMKSNAP1"  # wram + vram
MAGIC_V2 = b"SMKSNAP2"  # wram + vram + cgram

# A few WRAM addresses worth naming in output (extend as needed). These are
# direct-page / low-RAM offsets the project already tracks (see MEMORY.md).
KNOWN_WRAM = {
    0x36: "game state $36",
    0x32: "substate $32",
    0x44: "NMI flag $44",
    0xA4: "Mode-7 raster mode $A4",
    0x48: "fade step $48",
}


class Snap:
    __slots__ = ("wram", "vram", "cgram")

    def __init__(self, wram, vram, cgram):
        self.wram = wram
        self.vram = vram
        self.cgram = cgram  # may be b"" for v1 snapshots


def vram_region_tag(word_addr):
    """Annotate a VRAM word address with its Mode-7 role. In Mode 7 the
    128x128 tilemap lives in the LOW byte of words $0000-$3FFF and the 256-tile
    8bpp character data in the HIGH byte of the same words."""
    if word_addr < 0x4000:
        return "  [Mode-7 map(lo)+char(hi)]"
    return ""


def load_snapshot(path):
    with open(path, "rb") as f:
        data = f.read()
    magic = data[:8]
    if magic == MAGIC_V1:
        wram_size, vram_size = struct.unpack_from("<II", data, 8)
        off = 16
        cgram_size = 0
    elif magic == MAGIC_V2:
        wram_size, vram_size, cgram_size = struct.unpack_from("<III", data, 8)
        off = 20
    else:
        raise ValueError(f"{path}: bad magic {magic!r}")
    wram = data[off:off + wram_size]; off += wram_size
    vram = data[off:off + vram_size]; off += vram_size
    cgram = data[off:off + cgram_size] if cgram_size else b""
    if len(wram) != wram_size or len(vram) != vram_size:
        raise ValueError(f"{path}: truncated")
    return Snap(wram, vram, cgram)


def frame_index(path):
    m = re.search(r"_f(\d+)\.bin$", path)
    return int(m.group(1)) if m else -1


def collect(prefix):
    """Map frame_no -> filepath. A direct .bin file (e.g. a Mesen2 reference
    dump) maps to a single synthetic frame 0; a prefix globs the _fNNN stream."""
    if os.path.isfile(prefix):
        return {0: prefix}
    files = glob.glob(prefix + "_f*.bin")
    out = {}
    for p in files:
        fi = frame_index(p)
        if fi >= 0:
            out[fi] = p
    return out


def first_diff(a, b, ignore=None):
    """First differing index of two equal-length byte strings, or -1.

    ignore: optional list of (lo, hi) inclusive byte ranges to skip (e.g. the
    CPU stack page, whose pushed-then-popped scratch is semantically dead — a
    recompiled function that runs in zero emulated time can leave different
    dead-stack bytes from sub-frame interrupt timing without any functional
    difference)."""
    n = min(len(a), len(b))
    # Fast path only when nothing is ignored.
    if ignore is None and a[:n] == b[:n]:
        return -1 if len(a) == len(b) else n
    blk = 4096
    i = 0
    while i < n:
        j = min(i + blk, n)
        if a[i:j] != b[i:j]:
            for k in range(i, j):
                if a[k] != b[k]:
                    if ignore and any(lo <= k <= hi for lo, hi in ignore):
                        continue
                    return k
        i = j
    return -1 if (len(a) == len(b) or ignore) else n


def diverging_regions(a, b, word=False):
    """List of (start, end_exclusive) byte/word ranges that differ."""
    regions = []
    n = min(len(a), len(b))
    step = 2 if word else 1
    in_run = False
    start = 0
    i = 0
    while i < n:
        if word:
            da = a[i] | (a[i + 1] << 8) if i + 1 < n else a[i]
            db = b[i] | (b[i + 1] << 8) if i + 1 < n else b[i]
            differ = da != db
        else:
            differ = a[i] != b[i]
        if differ and not in_run:
            in_run, start = True, i
        elif not differ and in_run:
            in_run = False
            regions.append((start // step, i // step))
        i += step
    if in_run:
        regions.append((start // step, n // step))
    return regions


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref_prefix", help="reference snapshot prefix (e.g. snap/ref)")
    ap.add_argument("test_prefix", help="test snapshot prefix (e.g. snap/recomp)")
    ap.add_argument("--max-regions", type=int, default=20,
                    help="max diverging regions to print on first bad frame")
    ap.add_argument("--stop-on-first", action="store_true",
                    help="stop after the first diverging frame")
    ap.add_argument("--vram-only", action="store_true",
                    help="ignore WRAM; compare only VRAM + CGRAM (Mode-7 focus)")
    ap.add_argument("--ignore-wram", default=None,
                    help="comma-separated hex WRAM ranges to ignore, e.g. "
                         "'1F00-1FFF' (the stack page — masks dead-stack scratch "
                         "so a functionally-faithful recomp gates green)")
    args = ap.parse_args()

    ignore_wram = None
    if args.ignore_wram:
        ignore_wram = []
        for part in args.ignore_wram.split(","):
            lo, hi = part.split("-")
            ignore_wram.append((int(lo, 16), int(hi, 16)))

    ref = collect(args.ref_prefix)
    test = collect(args.test_prefix)
    common = sorted(set(ref) & set(test))
    if not common:
        print("No overlapping frames between the two prefixes.", file=sys.stderr)
        print(f"  ref frames: {len(ref)}  test frames: {len(test)}", file=sys.stderr)
        return 2

    print(f"Comparing {len(common)} frames "
          f"[{common[0]}..{common[-1]}]  ref={args.ref_prefix}  test={args.test_prefix}\n")

    first_bad = None
    for fno in common:
        r = load_snapshot(ref[fno])
        t = load_snapshot(test[fno])
        wd = -1 if args.vram_only else first_diff(r.wram, t.wram, ignore_wram)
        vd = first_diff(r.vram, t.vram)
        cd = first_diff(r.cgram, t.cgram) if (r.cgram and t.cgram) else -1
        if wd < 0 and vd < 0 and cd < 0:
            continue
        if first_bad is None:
            first_bad = fno
        parts = []
        if wd >= 0:
            label = KNOWN_WRAM.get(wd, "")
            label = f" ({label})" if label else ""
            parts.append(f"WRAM @ ${wd:05X}{label}: ref={r.wram[wd]:02X} test={t.wram[wd]:02X}")
        if vd >= 0:
            word = vd & ~1
            ra = r.vram[word] | (r.vram[word + 1] << 8)
            ta = t.vram[word] | (t.vram[word + 1] << 8)
            parts.append(f"VRAM word ${word // 2:04X}: ref={ra:04X} test={ta:04X}")
        if cd >= 0:
            word = cd & ~1
            ra = r.cgram[word] | (r.cgram[word + 1] << 8)
            ta = t.cgram[word] | (t.cgram[word + 1] << 8)
            parts.append(f"CGRAM color ${word // 2:02X}: ref={ra:04X} test={ta:04X}")
        print(f"frame {fno:6d}  DIVERGES  " + "  |  ".join(parts))

        if fno == first_bad:
            # Detailed region report on the first bad frame.
            print(f"    -- first divergence detail (frame {fno}) --")
            if not args.vram_only:
                wregions = diverging_regions(r.wram, t.wram, word=False)
                print(f"    WRAM diverging regions: {len(wregions)}")
                for s, e in wregions[:args.max_regions]:
                    named = KNOWN_WRAM.get(s, "")
                    named = f"  {named}" if named else ""
                    print(f"      ${s:05X}-${e - 1:05X} ({e - s} bytes){named}")
                if len(wregions) > args.max_regions:
                    print(f"      ... +{len(wregions) - args.max_regions} more")
            vregions = diverging_regions(r.vram, t.vram, word=True)
            print(f"    VRAM diverging word-regions: {len(vregions)}  "
                  f"(total {sum(e - s for s, e in vregions)} words)")
            for s, e in vregions[:args.max_regions]:
                tag = vram_region_tag(s)
                print(f"      word ${s:04X}-${e - 1:04X} ({e - s} words){tag}")
            if len(vregions) > args.max_regions:
                print(f"      ... +{len(vregions) - args.max_regions} more")
            if r.cgram and t.cgram:
                cregions = diverging_regions(r.cgram, t.cgram, word=True)
                print(f"    CGRAM diverging color-regions: {len(cregions)}")
                for s, e in cregions[:args.max_regions]:
                    print(f"      color ${s:02X}-${e - 1:02X} ({e - s} colors)")
            print()
            if args.stop_on_first:
                break

    if first_bad is None:
        print("No divergence across all compared frames. "
              "Reference and test are byte-identical.")
        return 0
    print(f"\nFirst divergence at frame {first_bad}.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
