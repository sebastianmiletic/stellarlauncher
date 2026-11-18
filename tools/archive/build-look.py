#!/usr/bin/env python3
"""Build Archive Look from original artwork using librsvg."""
import json
from pathlib import Path
import random
import subprocess
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[2]
GUI = "assets/minecraft/textures/gui/"
MARK = (ROOT / "program_info/archive-mark.svg").read_text()
CHEST = MARK[MARK.index("  <path"):MARK.index("</svg>")]


def rasterize(svg):
    return subprocess.check_output(
        ["rsvg-convert"],
        input=svg.encode(),
    )


def svg(width, height, body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">{body}</svg>'


def svg_text(x, y, size, color, label):
    return (
        f'<text x="{x}" y="{y}" font-family="Helvetica" font-weight="bold" '
        f'font-size="{size}" fill="{color}">{label}</text>'
    )


def panorama():
    # Use the same sky color on all six faces to avoid seams as it rotates.
    rng = random.Random(1928)
    for face in range(6):
        stars = "".join(
            f'<rect x="{rng.randrange(8, 248)}" y="{rng.randrange(8, 248)}" '
            f'width="1" height="1" fill="#b9c5f0" opacity="{rng.uniform(.17, .65):.2f}"/>'
            for _ in range(48)
        )
        yield face, rasterize(svg(256, 256, '<rect width="256" height="256" fill="#191d32"/>' + stars))


def build():
    pack = ROOT / "program_info/archive-look.zip"
    with ZipFile(pack, "w") as archive:
        def write(name, data):
            entry = ZipInfo(name)
            archive.writestr(entry, data, compress_type=ZIP_DEFLATED, compresslevel=9)

        write("pack.mcmeta", json.dumps({
            "pack": {
                "description": "Archive Launcher · Midnight Blue",
                "min_format": [69, 0],
                "max_format": [97, 1],
            },
        }, ensure_ascii=False))
        write("pack.png", rasterize(MARK.replace('width="512" height="512"', 'width="128" height="128"')))
        write("assets/minecraft/texts/splashes.txt", "Make something worth keeping!\n")

        title = (
            f'<g transform="translate(64 8) scale(.31)">{CHEST}</g>'
            + svg_text(256, 151, 139, "#373d78", "ARCHIVE")
            + svg_text(256, 140, 139, "#dbeaff", "ARCHIVE")
        )
        title_image = rasterize(svg(1024, 256, title))
        for name in ("minecraft", "minceraft"):
            write(f"{GUI}title/{name}.png", title_image)
        write(f"{GUI}title/edition.png", rasterize(svg(512, 64, svg_text(57, 45, 32, "#bcccf0", "MINECRAFT JAVA EDITION"))))

        for face, image in panorama():
            write(f"{GUI}title/background/panorama_{face}.png", image)

        for name, fill, border in (
            ("button", "#2b3151", "#58678f"),
            ("button_highlighted", "#474981", "#aabfff"),
            ("button_disabled", "#282b3c", "#40455c"),
        ):
            sprite = svg(
                200, 20,
                f'<rect x=".5" y=".5" width="199" height="19" rx="3.5" fill="{fill}" stroke="{border}"/>',
            )
            path = f"{GUI}sprites/widget/{name}.png"
            write(path, rasterize(sprite))
            write(path + ".mcmeta", json.dumps({
                "gui": {"scaling": {"type": "nine_slice", "width": 200, "height": 20, "border": 4}},
            }))


if __name__ == "__main__":
    build()
