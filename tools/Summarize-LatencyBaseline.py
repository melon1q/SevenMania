"""Summarize the stage-1 CPU/GPU baseline; submission timing is not scanout latency."""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
args = parser.parse_args()
manifest = json.loads((args.directory / "manifest.json").read_text(encoding="utf-8-sig"))
start = float(manifest["start"]) + 1
end = float(manifest["start"]) + float(manifest["duration"]) - 1
results = {}
def distribution(values):
    if not values:
        return None
    values = sorted(values)
    return {"mean": statistics.mean(values), "p95": values[min(len(values)-1, int(len(values)*.95))],
            "p99": values[min(len(values)-1, int(len(values)*.99))], "max": values[-1]}
for path in sorted(args.directory.glob("r*-*.csv")):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = [r for r in csv.DictReader(stream) if start <= float(r["playhead_s"]) <= end]
    if not rows:
        raise ValueError(f"No measured frames: {path}")
    log = path.with_suffix(".log").read_text(encoding="utf-8-sig")
    counters = {key: float(value) for key, value in re.findall(r"([a-z_]+)=([0-9.]+)", log)}
    result = {"cpu_frame_ms": distribution([float(r["frame_ms"]) for r in rows]),
              "cpu_interval_ms": distribution([float(r["interval_ms"]) for r in rows]), "counters": counters}
    result["cpu_updates_per_second"] = 1000 / result["cpu_interval_ms"]["mean"]
    result["gpu_submits_per_second"] = counters.get("submitted_frames", 0) / float(manifest["duration"])
    result["gpu_sample_mean_ms"] = counters.get("gpu_busy_ms", 0) / counters["gpu_samples"] if counters.get("gpu_samples", 0) else None
    if "-gpu-" in path.stem:
        submitted_at = []
        elapsed = 0.0
        for row in rows:
            elapsed += float(row["interval_ms"])
            if row["present_deferred"] == "0":
                submitted_at.append(elapsed)
        result["estimated_submit_gap_ms"] = distribution([b-a for a, b in zip(submitted_at, submitted_at[1:])])
    results[path.stem] = result
print(json.dumps({"manifest": manifest, "window": [start, end], "runs": results}, ensure_ascii=False, indent=2))
