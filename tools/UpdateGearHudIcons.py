"""Upgrade only the first two play gear slots, preserving the rest of a skin."""
from pathlib import Path
import argparse
import copy
import hashlib
import io
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def upgrade(data: bytes) -> bytes:
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if archive.testzip() is not None:
            raise ValueError("Invalid skin CRC")
        infos = archive.infolist()
        files = {info.filename: archive.read(info) for info in infos}
    manifest = json.loads(files["manifest.json"])
    play = json.loads(files["screens/play.json"])
    elements = play["elements"]
    by_id = {element["id"]: element for element in elements}
    for index, resource, filename in [(0, "hi-speed-slot", "hud-hi-speed.png"), (1, "gauge-mode-slot", "hud-gauge-mode.png")]:
        by_id[f"gear-icon-{index}"]["resource"] = resource
        path = "assets/images/" + filename
        manifest["resourcePaths"][resource] = path
        files[path] = (ROOT / "Assets" / filename).read_bytes()
    speed = by_id.get("gear-speed-reference")
    first = by_id["gear-icon-0"]
    second = by_id["gear-icon-1"]

    def body(element, icon):
        element.update(x=icon["x"], y=icon["y"] + 12, width=icon["width"], height=icon["height"] - 12)
        extensions = copy.deepcopy(element.get("extensions", {}))
        extensions.update(copy.deepcopy(icon.get("extensions", {})))
        element["extensions"] = extensions
        for layout in element["extensions"].get("gearLayouts", {}).values():
            layout["y"] += 12
    if speed is not None:
        body(speed, first)
    gauge = by_id.get("gear-gauge-mode")
    if gauge is None:
        gauge = dict(id="gear-gauge-mode", type="text", font="Segoe UI", fontSize=13,
                     text="NORMAL", binding="play.gaugeMode",
                     align="center", verticalAlign="center", tint=dict(r=24, g=28, b=33),
                     z=max(e.get("z", 0) for e in elements) + 1)
        elements.append(gauge)
    body(gauge, second)
    gauge.pop("fallback", None)
    gauge.update(binding="play.gaugeMode", text="NORMAL", **{"default": "NORMAL"})
    files["manifest.json"] = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    files["screens/play.json"] = json.dumps(play, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        for info in infos:
            archive.writestr(info, files.pop(info.filename))
        for name, content in files.items():
            archive.writestr(name, content, compress_type=zipfile.ZIP_DEFLATED)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="+", type=Path)
    args = parser.parse_args()
    backup = ROOT / "build-rmlui-song-list/Validation/GearHudIconBackup"
    backup.mkdir(parents=True, exist_ok=True)
    for path in args.packages:
        original = path.read_bytes()
        digest = hashlib.sha256(original).hexdigest()[:16]
        saved = backup / (path.stem + "-" + digest + ".semaskin")
        if not saved.exists():
            saved.write_bytes(original)
        updated = upgrade(original)
        temporary = path.with_suffix(path.suffix + ".gear-pending")
        if temporary.exists():
            raise FileExistsError(temporary)
        temporary.write_bytes(updated)
        temporary.replace(path)
        print("Updated:", path)


if __name__ == "__main__":
    main()
