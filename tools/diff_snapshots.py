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

MAGIC = b"SMKSNAP1"

# A few WRAM addresses worth naming in output (extend as needed). These are
# direct-page / low-RAM offsets the project already tracks (see MEMORY.md).
KNOWN_WRAM = {
    0x36: "game state $36",
    0x32: "substate $32",
    0x44: "NMI flag $44",
    0xA4: "Mode-7 raster mode $A4",
    0x48: "fade step $48",
}


def load_snapshot(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 16 or data[:8] != MAGIC:
        raise ValueError(f"{path}: bad magic / too short")
    wram_size, vram_size = struct.unpack_from("<II", data, 8)
    off = 16
    wram = data[off:off + wram_size]
    off += wram_size
    vram = data[off:off + vram_size]
    if len(wram) != wram_size or len(vram) != vram_size:
        raise ValueError(f"{path}: truncated ({len(wram)}/{wram_size} wram, "
                         f"{len(vram)}/{vram_size} vram)")
    return wram, vram


def frame_index(path):
    m = re.search(r"_f(\d+)\.bin$", path)
    return int(m.group(1)) if m else -1


def collect(prefix):
    """Map frame_no -> filepath for a given snapshot prefix."""
    files = glob.glob(prefix + "_f*.bin")
    out = {}
    for p in files:
        fi = frame_index(p)
        if fi >= 0:
            out[fi] = p
    return out


def first_diff(a, b):
    """First differing index of two equal-length byte strings, or -1."""
    n = min(len(a), len(b))
    # Fast path: bulk compare, then narrow.
    if a[:n] == b[:n]:
        return -1 if len(a) == len(b) else n
    # Binary-narrow the first difference for speed on large buffers.
    lo, hi = 0, n
    # Find a differing block first.
    blk = 4096
    i = 0
    while i < n:
        j = min(i + blk, n)
        if a[i:j] != b[i:j]:
            for k in range(i, j):
                if a[k] != b[k]:
                    return k
        i = j
    return -1


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
    args = ap.parse_args()

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
        rw, rv = load_snapshot(ref[fno])
        tw, tv = load_snapshot(test[fno])
        wd = first_diff(rw, tw)
        vd = first_diff(rv, tv)
        if wd < 0 and vd < 0:
            continue
        if first_bad is None:
            first_bad = fno
        parts = []
        if wd >= 0:
            label = KNOWN_WRAM.get(wd, "")
            label = f" ({label})" if label else ""
            parts.append(f"WRAM @ ${wd:05X}{label}: ref={rw[wd]:02X} test={tw[wd]:02X}")
        if vd >= 0:
            word = vd & ~1
            ra = rv[word] | (rv[word + 1] << 8)
            ta = tv[word] | (tv[word + 1] << 8)
            parts.append(f"VRAM word ${word // 2:04X}: ref={ra:04X} test={ta:04X}")
        print(f"frame {fno:6d}  DIVERGES  " + "  |  ".join(parts))

        if fno == first_bad:
            # Detailed region report on the first bad frame.
            wregions = diverging_regions(rw, tw, word=False)
            vregions = diverging_regions(rv, tv, word=True)
            print(f"    -- first divergence detail (frame {fno}) --")
            print(f"    WRAM diverging regions: {len(wregions)}")
            for s, e in wregions[:args.max_regions]:
                named = KNOWN_WRAM.get(s, "")
                named = f"  {named}" if named else ""
                print(f"      ${s:05X}-${e - 1:05X} ({e - s} bytes){named}")
            if len(wregions) > args.max_regions:
                print(f"      ... +{len(wregions) - args.max_regions} more")
            print(f"    VRAM diverging word-regions: {len(vregions)}")
            for s, e in vregions[:args.max_regions]:
                print(f"      word ${s:04X}-${e - 1:04X} ({e - s} words)")
            if len(vregions) > args.max_regions:
                print(f"      ... +{len(vregions) - args.max_regions} more")
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
