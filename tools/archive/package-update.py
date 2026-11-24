#!/usr/bin/env python3
"""Build a Sparkle release archive and appcast using Stellar’s login-keychain key."""

import argparse
from datetime import datetime, timezone
from email.utils import format_datetime
from pathlib import Path
import plistlib
import subprocess
import re
import tempfile
from urllib.parse import urlsplit
from xml.etree import ElementTree as ET

SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", SPARKLE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--app", type=Path)
    source.add_argument("--archive", type=Path, help="Package from the Stellar downloads workflow")
    parser.add_argument("--sparkle-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--release-url", required=True, help="HTTPS release download directory for this version")
    args = parser.parse_args()
    url = urlsplit(args.release_url)
    if url.scheme != "https" or not url.netloc or url.username or url.password or url.query or url.fragment:
        parser.error("--release-url must be an HTTPS directory without credentials, a query or a fragment")

    public_key = subprocess.check_output(
        [args.sparkle_bin / "generate_keys", "--account", "archivelauncher", "-p"], text=True
    ).strip()
    architecture = "arm64"
    if args.archive:
        archive = args.archive.resolve()
        match = re.fullmatch(r"StellarLauncher-([0-9]+\.[0-9]+\.[0-9]+)-macOS-(arm64|x86_64)\.zip", archive.name)
        if not match:
            parser.error("--archive must be a Stellar macOS package from CI.")
        version, architecture = match.groups()

    feed = "appcast-intel.xml" if architecture == "x86_64" else "appcast.xml"
    appcast = args.output / feed
    args.output.mkdir(parents=True, exist_ok=True)
    if appcast.exists():
        parser.error(f"{appcast} already exists.")

    with tempfile.TemporaryDirectory(prefix="stellar-sign-") as extracted:
        if args.archive:
            subprocess.run(["ditto", "-x", "-k", archive, extracted], check=True)
            app = Path(extracted) / "Stellar Launcher.app"
        else:
            app = args.app.resolve()
        with (app / "Contents/Info.plist").open("rb") as plist:
            info = plistlib.load(plist)
        if info.get("SUPublicEDKey") != public_key or not info.get("SUFeedURL", "").endswith("/" + feed):
            parser.error("The package must use the release signing key and the correct architecture feed.")
        subprocess.run(["codesign", "--verify", "--deep", "--strict", "--all-architectures", app], check=True)
        if args.archive and info["CFBundleShortVersionString"] != version:
            parser.error("The package version does not match its archive name.")
        if not args.archive:
            version = info["CFBundleShortVersionString"]
            archive = args.output / f"StellarLauncher-{version}-macOS-arm64.zip"
            if archive.exists():
                parser.error(f"{archive} already exists.")
            subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive], check=True)

    sign_update = args.sparkle_bin / "sign_update"
    signature = subprocess.check_output([sign_update, "--account", "archivelauncher", "-p", archive], text=True).strip()
    subprocess.run([sign_update, "--account", "archivelauncher", "--verify", archive, signature], check=True)

    rss = ET.Element("rss", version="2.0")
    channel = ET.SubElement(rss, "channel")
    ET.SubElement(channel, "title").text = "Stellar Launcher updates"
    item = ET.SubElement(channel, "item")
    ET.SubElement(item, "title").text = f"Stellar Launcher {version}"
    ET.SubElement(item, f"{{{SPARKLE}}}version").text = info["CFBundleVersion"]
    ET.SubElement(item, f"{{{SPARKLE}}}shortVersionString").text = version
    ET.SubElement(item, "pubDate").text = format_datetime(datetime.now(timezone.utc))
    ET.SubElement(item, "enclosure", {
        "url": f"{args.release_url.rstrip('/')}/{archive.name}",
        f"{{{SPARKLE}}}edSignature": signature,
        "length": str(archive.stat().st_size),
        "type": "application/octet-stream",
    })
    ET.indent(rss)
    ET.ElementTree(rss).write(appcast, encoding="utf-8", xml_declaration=True)


if __name__ == "__main__":
    main()
