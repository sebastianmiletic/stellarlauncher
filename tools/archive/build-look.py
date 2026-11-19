#!/usr/bin/env python3
"""Build Stellar Look from original artwork using Pillow and librsvg."""
from io import BytesIO
import json
from pathlib import Path
import subprocess
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image


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


def write_panorama(write):
    background = f"{GUI}title/background/"
    dim = png(Image.new("RGBA", (1, 1), (0, 0, 0, 60)))

    with Image.open(WALLPAPER) as wallpaper:
        art = wallpaper.convert("L")
        # Earlier games use one static, center-framed image as the fallback.
        write(background + "panorama_overlay.png", png(art.resize((2560, 1440), Image.Resampling.LANCZOS)))
        # Java 26.3 uses an animated shader and samples the positive Z face.
        overlay = "stellar_26_3/"
        face = png(art.resize((FACE_SIZE, FACE_SIZE), Image.Resampling.LANCZOS))
        plain = png(Image.new("L", (FACE_SIZE, FACE_SIZE), 4))
        for index in range(6):
            write(overlay + background + f"panorama_{index}.png", face if index == 0 else plain)
        write(overlay + background + "panorama_overlay.png", dim)
        for shader in ("panorama.vsh", "panorama.fsh"):
            source = Path(__file__).with_name("panorama") / shader
            write(overlay + "assets/minecraft/shaders/core/" + shader, source.read_bytes())


def build():
    pack = ROOT / "program_info/archive-look.zip"
    with ZipFile(pack, "w") as archive:
        def write(name, data):
            entry = ZipInfo(name)
            archive.writestr(entry, data, compress_type=ZIP_DEFLATED, compresslevel=9)

        write("pack.mcmeta", json.dumps({
            "pack": {
                "description": "Stellar Launcher · Event Horizon",
                "min_format": [69, 0],
                "max_format": [97, 1],
            },
            "overlays": {
                "entries": [{"directory": "stellar_26_3", "min_format": [97, 1], "max_format": [97, 1]}],
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

        write_panorama(write)

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
