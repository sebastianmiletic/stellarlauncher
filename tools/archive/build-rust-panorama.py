#!/usr/bin/env python3
"""Project Stellar's artwork onto Horizon's stationary title-screen cube."""
import argparse
from io import BytesIO
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

import numpy as np
from PIL import Image


def build(wallpaper, destination):
    with Image.open(wallpaper) as picture:
        art = np.asarray(picture.convert("L"), dtype=np.float32)
    height, width = art.shape
    size = 1024
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    u, v = (x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1
    one = np.ones_like(u)
    # Resource filenames in vanilla's order: +Z, +X, -Z, -X, -Y, +Y.
    directions = [(u, -v, one), (one, -v, -u), (-u, -v, -one),
                  (-one, -v, u), (u, -one, -v), (u, one, v)]
    pitch = np.deg2rad(190)
    scale = 0.5 / np.tan(np.deg2rad(85 / 2))
    with ZipFile(destination, "w") as output:
        meta = {"pack": {"description": "Stellar · Stationary Rust title",
                         "min_format": [97, 1], "max_format": [97, 1]}}
        output.writestr(ZipInfo("pack.mcmeta"), json.dumps(meta), ZIP_DEFLATED)
        output.writestr(ZipInfo("assets/stellar/title/panorama_motion.txt"),
                        "stellar-1", ZIP_DEFLATED)
        strings = {"horizon.title.not_affiliated": ""}
        output.writestr(ZipInfo("assets/stellar/lang/en_us.json"),
                        json.dumps(strings), ZIP_DEFLATED)
        for index, (dx, dy, dz) in enumerate(directions):
            camera_y = dy * np.cos(pitch) - dz * np.sin(pitch)
            depth = -(dy * np.sin(pitch) + dz * np.cos(pitch))
            forward = np.maximum(depth, 0.0001)
            sample_x = 0.5 + dx / forward * scale / (16 / 9)
            sample_y = 0.54 - camera_y / forward * scale
            visible = (depth > 0) & (sample_x >= 0) & (sample_x <= 1)
            visible &= (sample_y >= 0) & (sample_y <= 1)
            sx = np.clip(sample_x, 0, 1) * (width - 1)
            sy = np.clip(sample_y, 0, 1) * (height - 1)
            ix, iy = sx.astype(np.int32), sy.astype(np.int32)
            fx, fy = sx - ix, sy - iy
            right, below = np.minimum(ix + 1, width - 1), np.minimum(iy + 1, height - 1)
            top = art[iy, ix] * (1 - fx) + art[iy, right] * fx
            bottom = art[below, ix] * (1 - fx) + art[below, right] * fx
            pixels = np.where(visible, top * (1 - fy) + bottom * fy, 0)
            # Horizon's cube uploader flips file rows before sampling.
            image = Image.fromarray(np.round(pixels[::-1]).astype(np.uint8))
            with BytesIO() as data:
                image.save(data, format="PNG", optimize=True)
                key = f"assets/minecraft/textures/gui/title/background/panorama_{index}.png"
                output.writestr(ZipInfo(key), data.getvalue(), ZIP_DEFLATED)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wallpaper", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.wallpaper, args.output)
