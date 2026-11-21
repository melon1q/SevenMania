#!/usr/bin/env python3
"""Add 5K / 10K / 14K / PMS gear geometry to a .semaskin play screen.

The 7K gear in screens/play.json is the base. For every native gear part this script writes
extensions.gearModes.<mode> = {x, y, width, height, visible} (plus fullWidth on the gauge fill),
authored with the gear centered at the left layout position (x center 342) exactly like the base
geometry, and adds the second-player parts (gear-*-p2-*) that DP / PMS need. Key textures are the
skin's own 7K white-key / black-key resources, so every mode uses the 7K button textures. Key guide
text elements follow their lanes; the game fills them with the lane's bound key name at run time.

Lane widths mirror LaneLayout (src/LaneLayout.h) and Main.cpp's DP sizing (key 40 / scratch 68),
so the skin gear lines up with the engine's notes. Idempotent: re-running replaces generated data.

Usage: python tools/generate-gear-modes.py <skin.semaskin> [output.semaskin]
"""
import copy
import json
import sys
import zipfile

CENTER = 342.0           # gear center at the left layout position
BASE_WIDTH = 492.0       # 7K gear panel width: 108 + 4*60 + 3*48
BASE_LEFT = CENTER - BASE_WIDTH / 2   # 96
KEY_Y, KEY_H = 731, 63
RIM_CENTER_Y = 719 + 47  # vertical center of the scratch rim

MODES = {
    # name: (players, keys, popn, key width, scratch width)
    "5k": (1, 5, False, 60, 108),
    "10k": (2, 5, False, 40, 68),
    "14k": (2, 7, False, 40, 68),
    "pms": (1, 7, True, 60, 60),
}


class Layout:
    def __init__(self, players, keys, popn, kw, sw):
        self.players, self.keys, self.popn, self.kw, self.sw = players, keys, popn, kw, sw

    def count(self):
        return 9 if self.popn else 16 if self.players == 2 else self.keys + 1

    def width(self, lane):
        if self.popn:
            return self.kw
        key = lane % 8
        if lane >= 8 and self.players == 1 or key > self.keys:
            return 0
        return self.sw if key == 0 else self.kw if key % 2 == 1 else self.kw * 4 // 5

    def position(self, lane):
        if self.popn or self.players != 2:
            return lane
        return 15 if lane == 8 else lane - 1 if lane > 8 else lane

    def lane_at(self, position):
        if self.popn or self.players != 2 or position < 8:
            return position
        return 8 if position == 15 else position + 1

    def total(self):
        return sum(self.width(l) for l in range(self.count()))

    def left(self, lane):
        return sum(self.width(self.lane_at(p)) for p in range(self.position(lane)))


def number(value):
    return int(value) if float(value).is_integer() else round(float(value), 4)


def entry(x, y, w, h, visible):
    return {"x": number(x), "y": number(y), "width": number(w), "height": number(h), "visible": visible}


def hidden(e):
    return entry(e["x"], e["y"], e["width"], e["height"], False)


def lane_for(kind, mode, name):
    """Lane an element id refers to in a mode, or None. kind: lane|separator|key|guide."""
    parts = name.split("-")
    second = "p2" in parts
    n = int(parts[-1])
    if mode == "pms":
        if kind == "separator":
            return 8 if second and n == 1 else None if second or n > 7 else n
        if second:
            return {2: 7, 3: 8}.get(n)
        return n - 1 if 1 <= n <= 7 else None
    return (8 if second else 0) + n


def generate(play):
    elements = play["elements"]
    by_id = {e["id"]: e for e in elements}

    # key guides are hidden once the song starts (play.keyGuide, see docs/SevenManiaSkin-Phase3-Implementation.md)
    for e in elements:
        if e["id"].startswith("gear-key-guide-"):
            e["binding"] = "play.keyGuide"

    # 1. second-player parts (hidden in the 7K base)
    def clone(source, new_id, **changes):
        e = copy.deepcopy(by_id[source])
        e["id"] = new_id
        e.update(changes)
        e["visible"] = False
        e["extensions"].pop("gearModes", None)
        return e

    additions = []   # (after_id, element)
    for i in range(0, 8):
        additions.append((f"gear-lane-{i}", clone(f"gear-lane-{i}", f"gear-lane-p2-{i}")))
        additions.append((f"gear-key-guide-{i}", clone(f"gear-key-guide-{i}", f"gear-key-guide-p2-{i}")))
    for i in range(1, 8):
        additions.append((f"gear-separator-{i}", clone(f"gear-separator-{i}", f"gear-separator-p2-{i}")))
        additions.append((f"gear-key-{i}", clone(f"gear-key-{i}", f"gear-key-p2-{i}")))
    additions.append(("gear-scratch-disc", clone("gear-scratch-rim", "gear-scratch-rim-p2")))
    additions.append(("gear-scratch-rim-p2", clone("gear-scratch-disc", "gear-scratch-disc-p2")))
    for after, e in additions:
        if e["id"] in by_id:
            continue
        index = next(i for i, v in enumerate(elements) if v["id"] == after)
        elements.insert(index + 1, e)
        by_id[e["id"]] = e

    # base geometry of the 14K layout for the p2 parts so they sit where DP puts them
    dp = Layout(*MODES["14k"])
    dp_left = CENTER - dp.total() / 2
    for e in elements:
        parts = e["id"].split("-")
        if "p2" not in parts or not e["id"].startswith("gear-"):
            continue
        kind = ("lane" if e["id"].startswith("gear-lane-") else "separator" if e["id"].startswith("gear-separator-")
                else "guide" if e["id"].startswith("gear-key-guide-") else "key" if e["id"].startswith("gear-key-p2") else "scratch")
        if kind == "scratch":
            continue
        lane = lane_for(kind, "14k", e["id"])
        apply_geometry(e, kind, dp, dp_left, lane)
    for rim, disc in (("gear-scratch-rim-p2", "gear-scratch-disc-p2"),):
        rect = scratch_rects(dp, dp_left, 8)
        for key, value in zip((rim, disc), rect):
            by_id[key].update({"x": value[0], "y": value[1], "width": value[2], "height": value[3]})

    # gear layouts per position for the new parts (same offsets as the other native parts)
    for e in elements:
        ext = e.setdefault("extensions", {})
        if "p2" in e["id"].split("-") and e["id"].startswith("gear-"):
            ext["followGear"] = True
            ext["nativeGearPart"] = True
            if e["id"].startswith(("gear-lane-", "gear-separator-")):
                ext["editorOnly"] = True
            ext["gearLayouts"] = {"left": {"x": e["x"], "y": e["y"]},
                                  "center": {"x": number(e["x"] + 618), "y": e["y"]},
                                  "right": {"x": number(e["x"] + 1236), "y": e["y"]}}

    # 2. per-mode geometry for every native gear part (and gear-following text)
    for name, spec in MODES.items():
        layout = Layout(*spec)
        left = CENTER - layout.total() / 2
        width = layout.total()
        for e in elements:
            ext = e.get("extensions", {})
            if not (ext.get("nativeGearPart") or ext.get("followGear")):
                continue
            modes = ext.setdefault("gearModes", {})
            modes[name] = mode_entry(e, name, layout, left, width)

    # keep z equal to list order (the editor does the same on save)
    for i, e in enumerate(elements):
        e["z"] = i


def scratch_rects(layout, left, lane):
    lane_left = left + layout.left(lane)
    size = layout.width(lane) - 10
    rim_x, rim_y = lane_left + 5, RIM_CENTER_Y - size / 2
    return (number(rim_x), number(rim_y), size, size), (number(rim_x + 7), number(rim_y + 7), size - 14, size - 14)


def apply_geometry(e, kind, layout, left, lane):
    width = layout.width(lane)
    lane_left = left + layout.left(lane)
    if kind == "lane":
        e.update(x=number(lane_left), width=width)
    elif kind == "guide":
        e.update(x=number(lane_left), width=width)
    elif kind == "separator":
        e.update(x=number(lane_left), width=1)
    elif kind == "key":
        black = e["resource"] == "black-key"
        inset, shrink = (7, 14) if black else (4, 8)
        e.update(x=number(lane_left + inset), width=width - shrink, y=KEY_Y, height=KEY_H)


def mode_entry(e, mode, layout, left, width):
    ident = e["id"]
    ext = e.get("extensions", {})
    total = layout.total()
    base = entry(e["x"], e["y"], e["width"], e["height"], e.get("visible", True))
    shift_left = left - BASE_LEFT
    if ident == "gear-panel" or ident == "gear-judgement-line":
        return entry(left, e["y"], total, e["height"], True)
    if ident == "gear-outer-border-left":
        return entry(left - 6.5, e["y"], e["width"], e["height"], True)
    if ident == "gear-outer-border-right":
        return entry(left + total + 1, e["y"], e["width"], e["height"], True)
    if ident == "gear-gauge-frame":
        return entry(left + 2, e["y"], total - 4, e["height"], True)
    if ident == "gear-gauge-background":
        return entry(left + 3, e["y"], total - 6, e["height"], True)
    if ident == "gear-gauge-interior":
        return entry(left + 4, e["y"], total - 8, e["height"], True)
    if ident == "gear-gauge-fill-reference":
        out = entry(left + 4, e["y"], (total - 8) * 0.2, e["height"], True)
        out["fullWidth"] = number(total - 8)
        return out
    if ident in ("gear-scratch-rim", "gear-scratch-disc", "gear-scratch-rim-p2", "gear-scratch-disc-p2"):
        second = ident.endswith("-p2")
        lane = 8 if second else 0
        if layout.popn or layout.width(lane) <= 0:
            return hidden(e)
        rim, disc = scratch_rects(layout, left, lane)
        x, y, w, h = rim if "rim" in ident else disc
        return entry(x, y, w, h, True)
    if ident.startswith("gear-icon-") or ident in ("gear-speed-reference", "gear-gauge-mode"):
        return base  # centered on the gear, no geometry change
    if ident in ("title", "score", "score-text", "gauge-label"):
        return entry(e["x"] + shift_left, e["y"], e["width"], e["height"], e.get("visible", True))
    if ident == "gauge":
        return entry(e["x"] + (left + total - (BASE_LEFT + BASE_WIDTH)), e["y"], e["width"], e["height"], e.get("visible", True))
    for prefix, kind in (("gear-lane-", "lane"), ("gear-separator-", "separator"), ("gear-key-guide-", "guide"), ("gear-key-", "key")):
        if ident.startswith(prefix):
            lane = lane_for(kind, mode, ident)
            if lane is None or layout.width(lane) <= 0 or (kind == "separator" and layout.position(lane) == 0):
                return hidden(e)
            probe = copy.deepcopy(e)
            apply_geometry(probe, kind, layout, left, lane)
            return entry(probe["x"], probe["y"], probe["width"], probe["height"], True)
    return base


def main():
    source = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else source
    with zipfile.ZipFile(source) as archive:
        entries = [(info.filename, archive.read(info.filename)) for info in archive.infolist()]
    play = None
    out = []
    for name, data in entries:
        if name == "screens/play.json":
            play = json.loads(data.decode("utf-8"))
            generate(play)
            data = json.dumps(play, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf-8")
        out.append((name, data))
    if play is None:
        raise SystemExit("screens/play.json not found")
    tmp = target + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_STORED) as archive:
        for name, data in out:
            archive.writestr(name, data)
    import os
    os.replace(tmp, target)
    print(f"wrote {target}: {len(play['elements'])} play elements")


if __name__ == "__main__":
    main()
