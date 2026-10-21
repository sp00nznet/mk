#!/usr/bin/env python3
"""Run a fresh ROM oracle and native closure on the same deterministic route.

Outputs are local research data. Use an ignored build directory for --output.
Every run gets a new directory; old snapshots cannot make a failed run pass.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from diff_snapshots import collect, load_snapshot, first_diff

SCRIPT = ('360:Y,360:START,420:START,480:START,540:START,600:START,'
          '660:START,720:START,780:START,840:START,900:START,960:START,'
          '1120-1399:B')
EXPANSION = '81F722,81FD22,8087D9,80879A'


def run(exe, rom, directory, frames, intercepts, semantic_check=False):
    directory.mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith('SMK_')}
    env.update(SMK_HEADLESS='1', SMK_MAX_FRAMES=str(frames), SMK_SCRIPT=SCRIPT,
               SMK_SNAPSHOT_EVERY='10', SMK_SNAPSHOT_PREFIX=str(directory / 'snap'))
    if intercepts is not None:
        env.update(SMK_INTERP='0', SMK_RECOMP='1', SMK_RECOMP_BUSPHASE='1',
                   SMK_RECOMP_PHASE_YIELD='1', SMK_RECOMP_INTERCEPTS=intercepts)
        if semantic_check:
            env['SMK_SEMANTIC_VERIFY'] = '1'
    with (directory / 'run.log').open('w', encoding='utf-8') as log:
        subprocess.run([str(exe), str(rom)], cwd=ROOT, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
    # MSVC console messages may use the active Windows code page. The counters
    # are ASCII; unrelated status text must not prevent validating the run.
    output = (directory / 'run.log').read_text(encoding='utf-8', errors='replace')
    hits = re.search(r'final intercept_hits=(\d+)', output)
    if intercepts is not None and (hits is None or int(hits[1]) == 0):
        raise RuntimeError('Native run had no completed interceptions; inspect run.log')
    snapshots = collect(str(directory / 'snap'))
    expected = set(range(10, frames + 1, 10))
    if set(snapshots) != expected:
        raise RuntimeError(f'Incomplete snapshot set in {directory}')
    semantic = {}
    if semantic_check and intercepts is not None:
        for address, entered, completed, unsupported, mismatches in re.findall(
                r'SEMANTIC ([0-9A-F]+) entered=(\d+) completed=(\d+) unsupported=(\d+) mismatches=(\d+)', output):
            semantic[address] = dict(entered=int(entered), completed=int(completed),
                                     unsupported=int(unsupported), mismatches=int(mismatches))
        if not sum(row['completed'] for row in semantic.values()):
            raise RuntimeError('No completed live semantic comparisons; inspect run.log')
        if any(row['mismatches'] for row in semantic.values()):
            raise RuntimeError('Live semantic mismatch; inspect run.log')
    return snapshots, int(hits[1]) if hits else 0, semantic


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build-manifest/Release/smk_launcher.exe')
    parser.add_argument('--rom', type=Path, default=ROOT / 'Super Mario Kart (USA).sfc')
    parser.add_argument('--output', type=Path, default=ROOT / 'build-manifest/closure-gates')
    parser.add_argument('--frames', type=int, default=1400)
    parser.add_argument('--intercepts', default=EXPANSION)
    parser.add_argument('--ignore-dead-stack', action='store_true')
    parser.add_argument('--semantic-check', action='store_true')
    args = parser.parse_args()
    if args.frames < 10 or args.frames % 10:
        parser.error('--frames must be a positive multiple of 10')
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='route-', dir=args.output.resolve()))
    print(f'Validation output: {directory}', flush=True)
    ref, _, _ = run(args.exe.resolve(), args.rom.resolve(), directory / 'reference', args.frames, None)
    native, hits, semantic = run(args.exe.resolve(), args.rom.resolve(), directory / 'native', args.frames, args.intercepts, args.semantic_check)
    ignored = [(0x1F00, 0x1FFF)] if args.ignore_dead_stack else []
    failures = []
    for frame in sorted(ref):
        a, b = load_snapshot(ref[frame]), load_snapshot(native[frame])
        for region in ('wram', 'vram', 'cgram'):
            left, right = getattr(a, region), getattr(b, region)
            offset = first_diff(left, right, ignored if region == 'wram' else None)
            if len(left) != len(right) or offset != -1:
                failures.append({'frame': frame, 'region': region, 'offset': offset})
    report = dict(rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),
                  executable_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                  frames=args.frames, samples=len(ref), script=SCRIPT,
                  intercepts=args.intercepts, intercept_hits=hits,
                  semantic=semantic,
                  ignore_wram=ignored, failures=failures, passed=not failures)
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
