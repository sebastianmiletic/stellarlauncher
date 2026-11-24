#!/usr/bin/env python3
"""Keep Daybreak's sky and let Horizon shade blocks with its own lightmap."""
import argparse
from pathlib import Path
import tomllib
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


MANIFEST = """schema = 1
id = "stellar_daybreak_sky"
name = "Daybreak Sky · Stellar"
version = "1.0.0"
api = "0.2"
description = "Daybreak skies with Horizon's normal terrain and Fullbright lightmap."
authors = ["hildlabs", "Stellar Launcher contributors"]
license = "MIT"
shared = ["common.wgsl"]

[sky]
file = "sky.wgsl"
name = "Sky"

[[options]]
id = "sky_style"
name = "Sky colour"
description = "Blend the vanilla sky with deeper blues and warm sunsets."
type = "float"
default = 0.6
min = 0.0
max = 1.0
step = 0.05
"""


def build(source, destination):
    with ZipFile(source) as daybreak, ZipFile(destination, "w") as pack:
        upstream = tomllib.loads(daybreak.read("horizon-shaders.toml").decode())
        if upstream["id"] != "daybreak" or upstream["version"] != "0.4.1":
            raise ValueError("Expected Daybreak 0.4.1")
        files = {
            "horizon-shaders.toml": MANIFEST.encode(),
            "README.md": (
                b"Daybreak 0.4.1 sky shaders by hildlabs, under the MIT license.\n"
                b"https://playhorizon.dev/mods/daybreak\n"
                b"Stellar uses the sky stage and omits custom material and post processing.\n"
            ),
        }
        for name in ("LICENSE", "sky.wgsl", "common.wgsl", "icon.png"):
            files[name] = daybreak.read(name)
        for name, data in files.items():
            pack.writestr(ZipInfo(name), data, compress_type=ZIP_DEFLATED)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("daybreak", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.daybreak, args.output)
