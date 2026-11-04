"""Compare paired Reflex runs. CPU submission proxies cannot approve latency benefit."""
import argparse
import csv
import json
import math
import re
import statistics
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError('Missing or invalid timing samples')
    return {key: values[max(0, math.ceil(len(values) * quantile) - 1)]
            for key, quantile in [('p95', .95), ('p99', .99)]} | {
                'mean': statistics.mean(values), 'max': values[-1], 'samples': len(values)}


def summarize(directory):
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8-sig'))
    start = float(manifest['start']) + 1
    end = float(manifest['start']) + float(manifest['duration']) - 1
    repeats = int(manifest['repeats'])
    if repeats < 2 or end <= start:
        raise ValueError('Need repeated runs and a nonempty analysis window')
    runs = {}
    failures = []
    for spec in manifest['runs']:
        name = spec['name']
        if name in runs:
            raise ValueError(f'Duplicate run: {name}')
        with (directory / f'{name}.csv').open(encoding='utf-8-sig', newline='') as stream:
            rows = [r for r in csv.DictReader(stream) if start <= float(r['playhead_s']) <= end]
        if not rows or float(rows[0]['playhead_s']) > start + .5 or float(rows[-1]['playhead_s']) < end - .5:
            raise ValueError(f'Incomplete measurement window: {name}')
        log = (directory / f'{name}.log').read_text(encoding='utf-8-sig')
        counters = {k: float(v) for k, v in re.findall(r'([A-Za-z_]+)=([0-9.]+)', log)}
        required = {'reflex_active': int(spec['mode'] == 'reflex'),
                    'driver_low_latency': int(spec['mode'] == 'reflex'),
                    'fallback': 0, 'audio_render_failures': 0, 'audio_empty_padding_events': 0}
        for key, expected in required.items():
            if counters.get(key) != expected:
                failures.append(f'{name}: {key}={counters.get(key)} expected {expected}')
        if spec['exitCode'] != 0:
            failures.append(f'{name}: profiler exit {spec["exitCode"]}')
        elapsed = 0
        submitted = []
        for row in rows:
            elapsed += float(row['interval_ms'])
            if row['present_deferred'] == '0':
                submitted.append(elapsed)
        runs[name] = {
            'cpu_frame_ms': distribution([float(r['frame_ms']) for r in rows]),
            'cpu_interval_ms': distribution([float(r['interval_ms']) for r in rows]),
            'estimated_submit_gap_ms': distribution([b-a for a, b in zip(submitted, submitted[1:])]),
            'counters': counters}
    comparisons = {}
    for fps in ('unlimited', '240', 'vsync'):
        names = {mode: [f'r{r}-gpu-{fps}-{mode}' for r in range(1, repeats + 1)] for mode in ('off', 'reflex')}
        if any(name not in runs for group in names.values() for name in group):
            raise ValueError(f'Incomplete matrix: {fps}')
        comparison = {}
        for metric in ('cpu_frame_ms', 'cpu_interval_ms', 'estimated_submit_gap_ms'):
            off = [runs[name][metric]['p99'] for name in names['off']]
            on = [runs[name][metric]['p99'] for name in names['reflex']]
            baseline = statistics.mean(off)
            allowance = max(float(manifest['allowedRegressionFraction']) * baseline, max(off)-min(off))
            regression = statistics.mean(on) - baseline
            passed = regression <= allowance
            comparison[metric] = {'off_p99_mean_ms': baseline, 'on_p99_mean_ms': statistics.mean(on),
                                  'allowed_increase_ms': allowance, 'passed': passed}
            if not passed:
                failures.append(f'{fps}: {metric} p99 regression exceeds allowance')
        comparisons[fps] = comparison
    return {'manifest': manifest, 'window': [start, end], 'runs': runs,
            'comparisons': comparisons, 'failures': failures,
            'stability_and_timing_passed': not failures,
            'release_approved': False,
            'remaining_gates': ['physical input-to-photon improvement beyond repeat error',
                                'manual listening and judgement verification',
                                'Alt+Tab, monitor change and real device removal checks']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = summarize(args.directory)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(0 if result['stability_and_timing_passed'] else 1)
