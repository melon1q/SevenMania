#!/usr/bin/env python3
"""Re-lay the 7K base gear of a .semaskin for the current LaneLayout widths (108 / 60 / 48).

screens/play.json stores the 7K gear as absolute geometry. After the lane widths change, run this
once on the skin, then tools/generate-gear-modes.py to refresh the other key modes.
Usage: python tools/retarget-7k-gear.py <skin.semaskin> [output.semaskin]
"""
import importlib.util
import json
import os
import sys
import zipfile

spec = importlib.util.spec_from_file_location("gear", os.path.join(os.path.dirname(os.path.abspath(__file__)), "generate-gear-modes.py"))
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

OLD_LEFT, OLD_WIDTH = 64.0, 556.0
LAYOUT = g.Layout(1, 7, False, 60, 108)
LEFT = g.CENTER - LAYOUT.total() / 2
TOTAL = LAYOUT.total()
SHIFT_WITH_LEFT = ("title", "score", "score-text", "gauge-label")


def retarget(play):
    elements = play["elements"]
    panel = next(e for e in elements if e["id"] == "gear-panel")
    if abs(panel["width"] - TOTAL) < 1e-6:
        print("already retargeted")
        return
    for e in elements:
        ident, parts = e["id"], e["id"].split("-")
        if "p2" in parts:
            continue
        if ident.startswith("gear-"):
            new = g.mode_entry(e, "7k", LAYOUT, LEFT, TOTAL)
        elif ident in SHIFT_WITH_LEFT:
            new = g.entry(e["x"] + LEFT - OLD_LEFT, e["y"], e["width"], e["height"], True)
        elif ident == "gauge":
            new = g.entry(e["x"] + (LEFT + TOTAL) - (OLD_LEFT + OLD_WIDTH), e["y"], e["width"], e["height"], True)
        else:
            continue
        dx, dy = new["x"] - e["x"], new["y"] - e["y"]
        for key in ("x", "y", "width", "height"):
            e[key] = new[key]
        layouts = e.get("extensions", {}).get("gearLayouts", {})
        for layout in layouts.values():
            layout["x"] = g.number(layout["x"] + dx)
            layout["y"] = g.number(layout["y"] + dy)


def main():
    source = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else source
    with zipfile.ZipFile(source) as archive:
        entries = [(i.filename, archive.read(i.filename)) for i in archive.infolist()]
    out = []
    for name, data in entries:
        if name == "screens/play.json":
            play = json.loads(data.decode("utf-8"))
            retarget(play)
            data = json.dumps(play, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf-8")
        out.append((name, data))
    tmp = target + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_STORED) as archive:
        for name, data in out:
            archive.writestr(name, data)
    os.replace(tmp, target)
    print("wrote", target)


if __name__ == "__main__":
    main()
