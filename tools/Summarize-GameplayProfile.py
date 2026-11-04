"""Summarize opt-in gameplay CSVs. Timings inside parent scopes overlap."""
import argparse
import csv
import json
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("csv", type=Path, nargs="+")
parser.add_argument("--start", type=float, default=31)
parser.add_argument("--end", type=float, default=37)
args = parser.parse_args()
summary = {}
for path in args.csv:
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = [row for row in csv.DictReader(stream) if args.start <= float(row["playhead_s"]) <= args.end]
    if not rows:
        parser.error(f"No frames in selected interval: {path}")
    result = {"frames": len(rows)}
    for key in rows[0]:
        if not key.endswith("_ms") and key not in ("visible_notes", "present_deferred"):
            continue
        values = sorted(float(row[key]) for row in rows)
        result[key] = {"mean": statistics.mean(values), "median": statistics.median(values),
            "p95": values[min(len(values) - 1, int(len(values) * .95))],
            "p99": values[min(len(values) - 1, int(len(values) * .99))]}
    result["render_loop_fps"] = 1000 / result["interval_ms"]["mean"]
    # Successful submit cadence is distinct from the much faster CPU render loop.
    submitted_at = []
    elapsed_ms = 0.0
    for row in rows:
        elapsed_ms += float(row["interval_ms"])
        if row.get("present_deferred") == "0":
            submitted_at.append(elapsed_ms)
    gaps = sorted(b - a for a, b in zip(submitted_at, submitted_at[1:]))
    if gaps:
        result["submission_interval_ms"] = {
            "mean": statistics.mean(gaps), "median": statistics.median(gaps),
            "p95": gaps[min(len(gaps) - 1, int(len(gaps) * .95))],
            "p99": gaps[min(len(gaps) - 1, int(len(gaps) * .99))], "max": max(gaps)}
    for state in ("0", "1"):
        values = [float(row["frame_ms"]) for row in rows if row["bga_rebuilt"] == state]
        if values:
            result[f"bga_rebuilt_{state}"] = {"frames": len(values), "frame_mean_ms": statistics.mean(values)}
    summary[path.name] = result
print(json.dumps(summary, indent=2))
