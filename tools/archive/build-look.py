#!/usr/bin/env python3
"""Build Stellar Look from original artwork using Pillow and librsvg."""
from io import BytesIO
import json
from pathlib import Path
import subprocess
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[2]
GUI = "assets/minecraft/textures/gui/"
MARK = (ROOT / "program_info/archive-mark.svg").read_text()
MARK_ART = MARK[MARK.index("  <defs>"):MARK.index("</svg>")]
WALLPAPER = ROOT / "program_info/stellar-wallpaper.jpg"
FACE_SIZE = 2048


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


def png(image):
    with BytesIO() as output:
        image.save(output, format="PNG", optimize=True)
        return output.getvalue()


def panorama():
    # Four views around the menu camera's orbit. The feathered edges match
    # the polar faces so adjacent cube faces meet without visible seams.
    sky = Image.new("L", (FACE_SIZE, FACE_SIZE), 4)
    with Image.open(WALLPAPER) as wallpaper:
        art = wallpaper.convert("L")
        art.thumbnail((FACE_SIZE, FACE_SIZE), Image.Resampling.LANCZOS)

    def edge_fade(length, feather):
        return bytes(round(255 * min(1, min(i, length - 1 - i) / feather)) for i in range(length))

    fade_x = Image.frombytes("L", (art.width, 1), edge_fade(art.width, 320)).resize(art.size)
    fade_y = Image.frombytes("L", (1, art.height), edge_fade(art.height, 160)).resize(art.size)
    feather = ImageChops.multiply(fade_x, fade_y)
    center = ((FACE_SIZE - art.width) // 2, (FACE_SIZE - art.height) // 2 - 35)

    for face, roll in enumerate((0, -12, 9, -5)):
        image = sky.copy()
        image.paste(art, center, feather)
        if roll:
            image = image.rotate(roll, resample=Image.Resampling.BICUBIC, fillcolor=4)
        yield face, png(image)

    polar = png(sky)
    yield 4, polar
    yield 5, polar


def build():
    pack = ROOT / "program_info/archive-look.zip"
    with ZipFile(pack, "w") as archive:
        def write(name, data):
            entry = ZipInfo(name)
            archive.writestr(entry, data, compress_type=ZIP_DEFLATED, compresslevel=9)

        write("pack.mcmeta", json.dumps({
            "pack": {
                "description": "Stellar Launcher · Event Horizon Panorama",
                "min_format": [69, 0],
                "max_format": [97, 1],
            },
        }, ensure_ascii=False))
        write("pack.png", rasterize(MARK.replace('width="512" height="512"', 'width="128" height="128"')))
        write("assets/minecraft/texts/splashes.txt", "")

        title = (
            f'<g transform="translate(64 8) scale(.31)">{MARK_ART}</g>'
            + svg_text(256, 151, 139, "#4b4b4b", "STELLAR")
            + svg_text(256, 140, 139, "#f1f1f1", "STELLAR")
        )
        title_image = rasterize(svg(1024, 256, title))
        for name in ("minecraft", "minceraft"):
            write(f"{GUI}title/{name}.png", title_image)
        write(f"{GUI}title/edition.png", rasterize(svg(512, 64, svg_text(57, 45, 32, "#cdcdcd", "MINECRAFT JAVA EDITION"))))

        for face, image in panorama():
            write(f"{GUI}title/background/panorama_{face}.png", image)
        # A subtle veil keeps the white menu labels readable over the disk.
        write(f"{GUI}title/background/panorama_overlay.png", png(Image.new("RGBA", (1, 1), (0, 0, 0, 60))))

        for name, fill, border in (
            ("button", "#222222", "#626262"),
            ("button_highlighted", "#3d3d3d", "#ededed"),
            ("button_disabled", "#181818", "#414141"),
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
