#!/usr/bin/env python3
"""Package the Stellar client preset. Minecraft and mods are downloaded by Prism."""
import argparse
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = {
    "formatVersion": 1,
    "game": "minecraft",
    "versionId": "1.0.0",
    "name": "Stellar",
    "summary": "A monochrome animated title screen and Lithium for Minecraft Java.",
    "dependencies": {"minecraft": "26.3", "fabric-loader": "0.20.1"},
    "files": [{
        "path": "mods/lithium-fabric-0.26.3+mc26.3.jar",
        "hashes": {
            "sha1": "2d3afe9d43bb2549e23479d8d93e644ff366bebf",
            "sha512": "2ca92fb530135d2baddf0ba4cc4f93976234876a38c4aa02b487b2552400f005afcfbeebf5373fb4396fe2020537a42d7597b8da5a52b904355ef51d5fef07ad",
        },
        "downloads": ["https://cdn.modrinth.com/data/gvQqBUqZ/versions/fWv5bnZk/lithium-fabric-0.26.3%2Bmc26.3.jar"],
        "fileSize": 919892,
        "env": {"client": "required", "server": "optional"},
    }],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(args.output, "w") as archive:
        manifest = json.dumps(MANIFEST, indent=2) + "\n"
        resource_pack = (ROOT / "program_info/archive-look.zip").read_bytes()
        options = 'resourcePacks:["vanilla","file/Stellar Look.zip"]\npanoramaScrollSpeed:1.0\n'
        for name, content in {
            "modrinth.index.json": manifest,
            "client-overrides/options.txt": options,
            "client-overrides/resourcepacks/Stellar Look.zip": resource_pack,
        }.items():
            archive.writestr(ZipInfo(name), content, compress_type=ZIP_DEFLATED)


if __name__ == "__main__":
    main()
