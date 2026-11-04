"""Offline analysis of matched real-engine upload/pool diagnostic captures."""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    return {
        "n": len(values), "mean": statistics.mean(values),
        **{name: values[int((len(values) - 1) * q)]
           for name, q in [("p95", .95), ("p99", .99), ("p999", .999)]},
        "max": values[-1],
    }


def analyze(manifest):
    base = str(manifest).removesuffix(".manifest.json")
    config = json.loads(manifest.read_text(encoding="utf-8"))
    meta = json.loads(Path(base + ".meta.json").read_text())
    with open(base + ".frames.csv") as f:
        all_rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]
    rows = [r for r in all_rows if config["analyze_wall_start"] <= r["wall_s"] <= config["analyze_wall_end"]]
    intervals = sorted((r["interval_ms"] for r in rows), reverse=True)
    # Same definition as the earlier Reflex studies: inverse mean of slowest 1%.
    low = 1000 / statistics.mean(intervals[:max(1, len(intervals) // 100)])
    stages = ["uploadhybrid_cpu_ms", "upload_backdrop_cpu_ms", "backdrop_compare_cpu_ms",
              "backdrop_copy_cpu_ms", "backdrop_retain_cpu_ms", "video_frame_free_cpu_ms",
              "upload_region0_cpu_ms", "upload_region1_cpu_ms", "upload_region2_cpu_ms",
              "present_cpu_ms", "d2d_end_cpu_ms"]
    result = {
        "label": config["label"], "config": config, "meta": meta,
        "cpu": distribution([r["frame_cpu_ms"] for r in rows]),
        "interval": distribution(intervals), "one_percent_low_fps": low,
        # Both include-zero per-frame cost and nonzero scope/call distributions.
        "stages": {k: {"per_frame": distribution([r[k] for r in rows]),
                       "calls": distribution([r[k] for r in rows if r[k] > 0])}
                   for k in stages},
        "counts": {k: sum(r.get(k, 0) for r in rows) for k in
                   ["backdrop_copy_calls", "backdrop_upload_bytes", "backdrop_compared_rows",
                    "video_frame_frees", "video_frame_freed_bytes", "paused_paints",
                    "unfocused_paints", "auto_resumes", "span_dropped",
                    "region0_bytes", "region1_bytes", "region2_bytes"]},
        "over_2ms": sum(r["interval_ms"] >= 2 for r in rows),
        "over_5ms": sum(r["interval_ms"] >= 5 for r in rows),
    }
    # UI frees are recorded only from that UI thread. Verify the expensive scope
    # really contains vector deallocation instead of inferring from shared_ptr.
    tail = sorted(rows, key=lambda r: r["frame_cpu_ms"], reverse=True)[:max(1, len(rows) // 100)]
    result["slow_cpu"] = {k: statistics.mean(r[k] for r in tail) for k in stages}
    result["slow_cpu"]["frame_cpu_ms"] = statistics.mean(r["frame_cpu_ms"] for r in tail)
    log = Path(base + ".log").read_text(encoding="utf-8")
    matches = re.findall(r"video_frames_created=(\d+) reused=(\d+) overflow=(\d+) pixel_allocations=(\d+) destroyed=(\d+) live_frame_bytes=(\d+) peak_frame_bytes=(\d+)", log)
    if matches:
        result["video_metrics_whole_capture"] = dict(zip(
            ["created", "reused", "overflow", "pixel_allocations", "destroyed", "live_frame_bytes", "peak_frame_bytes"],
            map(int, matches[-1])))
    case = config.get("bga_case", "original")
    result["foreground_steady"] = not any(result["counts"][k] for k in
                                          ["paused_paints", "unfocused_paints", "auto_resumes"])
    result["bga_workload_verified"] = case == "original" or (
        f"bga_case={case} " in log and
        (case == "off" or "bga_fixture_valid=1" in log) and
        (case != "still" or result.get("video_metrics_whole_capture", {}).get("pixel_allocations") == 0))
    memory = Path(base + ".memory.csv")
    if memory.exists():
        with memory.open() as f:
            samples = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]
        result["memory"] = {"samples": samples}
        if samples:
            result["memory"].update(private_min=min(r["private_bytes"] for r in samples),
                                    private_max=max(r["private_bytes"] for r in samples),
                                    private_first=samples[0]["private_bytes"],
                                    private_last=samples[-1]["private_bytes"])
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=Path("build-rmlui-song-list/Validation/HudFrameStudy"))
    parser.add_argument("--prefix", default="hybrid-")
    args = parser.parse_args()
    results = []
    for manifest in sorted(args.directory.glob(args.prefix + "*.manifest.json")):
        if not Path(str(manifest).replace(".manifest.json", ".meta.json")).exists():
            continue
        result = analyze(manifest)
        results.append(result)
        print(result["label"], "CPU P99/max", result["cpu"]["p99"], result["cpu"]["max"],
              "1% low", round(result["one_percent_low_fps"], 1), "upload calls", result["stages"]["uploadhybrid_cpu_ms"]["calls"],
              "video", result.get("video_metrics_whole_capture"), "BGA workload verified", result["bga_workload_verified"], flush=True)
    (args.directory / "hybrid-upload-summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
