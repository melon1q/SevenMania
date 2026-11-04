"""Export generated, synthetic compatibility fixtures as separate song folders.

Run BmsCompatibilityTests and BmsCompatibilityPlaybackTests first.
No personal Songs, settings, captures, or third-party music are copied.
"""
from pathlib import Path
import argparse
import shutil


def export(source: Path, destination: Path) -> None:
    source = source.resolve(strict=True)
    destination.mkdir(parents=True, exist_ok=True)
    assets = destination / "Assets"
    assets.mkdir(exist_ok=True)
    media = ("tone.wav", "base.bmp", "layer.bmp", "poor.bmp", "movie.avi", "offset.avi")
    for name in media:
        shutil.copyfile(source / name, assets / name)
    charts = sorted(p for p in source.iterdir() if p.suffix.lower() in (".bms", ".bme", ".bml", ".pms", ".bmson"))
    for chart in charts:
        folder = destination / chart.stem
        folder.mkdir(exist_ok=True)
        data = chart.read_bytes()
        # MATERIALS deliberately resolves beside the executable. Rewrite
        # only ordinary references, retaining shared WAV/BMP declarations.
        shared_wav = b"#MATERIALSWAV" in data or b"<bms-compatibility-fixture>" in data
        shared_bmp = b"#MATERIALSBMP" in data
        if b"#PATH_WAV audio" in data:
            data = data.replace(b"#PATH_WAV audio", b"#PATH_WAV ../Assets")
        for name in media:
            if name == "tone.wav" and (shared_wav or b"#PATH_WAV ../Assets" in data):
                continue
            if name.endswith(".bmp") and shared_bmp:
                continue
            data = data.replace(name.encode("ascii"), ("../Assets/" + name).encode("ascii"))
        (folder / chart.name).write_bytes(data)
    material = destination / "Materials" / "bms-compatibility-fixture"
    material.mkdir(parents=True, exist_ok=True)
    for name in ("tone.wav", "base.bmp"):
        shutil.copyfile(source / name, material / name)
    print(f"Exported {len(charts)} synthetic charts to {destination.resolve()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    export(args.source, args.destination)
