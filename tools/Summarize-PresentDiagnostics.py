"""Summarize SevenMania presentation diagnostics by A/B condition.

Reads %APPDATA%\\BmsPlayer\\presentation-diagnostics.csv (+ -hitches.csv) and groups
plays by (chart, variant, vsync, target_fps). Compare conditions only on the same
binary, chart and settings, run in alternating order (A B A B A B).

    python -I tools/Summarize-PresentDiagnostics.py [--chart dive_l7.bme] [--dir PATH]
"""
import argparse
import csv
import os
import statistics
from collections import Counter, defaultdict
from pathlib import Path


def fnum(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dir', default=os.path.join(os.environ.get('APPDATA', ''), 'BmsPlayer'))
    parser.add_argument('--chart', help='only this chart file name')
    args = parser.parse_args()
    folder = Path(args.dir)
    summary_file = folder / 'presentation-diagnostics.csv'
    hitch_file = folder / 'presentation-diagnostics-hitches.csv'
    if not summary_file.exists():
        raise SystemExit(f'not found: {summary_file}')
    plays = [r for r in csv.DictReader(summary_file.open(encoding='utf-8'))
             if fnum(r.get('gpu_submits')) + fnum(r.get('gdi_frames')) > 0 and fnum(r.get('seconds')) >= 10]
    if args.chart:
        plays = [r for r in plays if r['chart'] == args.chart]
    if 'run_id' not in (plays[0] if plays else {}):
        print('note: this file predates variant/run_id columns; every play is grouped as "default".')

    hitches = defaultdict(list)
    if hitch_file.exists():
        for r in csv.DictReader(hitch_file.open(encoding='utf-8')):
            if r.get('run_id'):
                hitches[r['run_id']].append(r)

    print('Run order:')
    for r in plays:
        vsync = 'VSYNC' if r['vsync'] == '1' else f"cap {r['target_fps']}"
        print(f"  {r.get('run_id', '-'):>10}  {r['chart']:<24} {r.get('variant', 'default'):<28} {vsync:<9} "
              f"hitches {r['missed_intervals']:>4}  p99 {fnum(r['interval_p99_ms']):6.3f}  max {fnum(r['interval_max_ms']):6.2f}")

    groups = defaultdict(list)
    for r in plays:
        key = (r['chart'], r.get('variant', 'default'), 'VSYNC' if r['vsync'] == '1' else f"cap {r['target_fps']}")
        groups[key].append(r)

    print('\nBy condition (mean [min..max] over plays):')
    for (chart, variant, mode), rows in sorted(groups.items()):
        missed = [fnum(r['missed_intervals']) for r in rows]
        ratio = [fnum(r['missed_intervals']) / max(1.0, fnum(r['gpu_submits'])) * 100 for r in rows]
        p99 = [fnum(r['interval_p99_ms']) for r in rows]
        worst = [fnum(r['interval_max_ms']) for r in rows]
        causes = Counter()
        full_upload = total = 0
        for r in rows:
            frame_bytes = fnum(r['width']) * fnum(r['height']) * 4
            for h in hitches.get(r.get('run_id', ''), []):
                causes[h['cause']] += 1
                total += 1
                full_upload += fnum(h['upload_bytes']) >= frame_bytes
        print(f"  {chart} | {variant} | {mode} | plays {len(rows)}")
        print(f"    hitches {statistics.mean(missed):7.1f} [{min(missed):.0f}..{max(missed):.0f}]"
              f"  ({statistics.mean(ratio):.2f}% of submits)")
        print(f"    p99 {statistics.mean(p99):6.3f} ms [{min(p99):.3f}..{max(p99):.3f}]   max {max(worst):6.2f} ms")
        if total:
            print(f"    causes {dict(causes.most_common())}")
            print(f"    hitch windows with a full-frame upload: {full_upload}/{total} ({full_upload / total:.0%})")
        if len(rows) < 3:
            print('    (fewer than 3 plays: treat as indicative only)')


if __name__ == '__main__':
    main()
