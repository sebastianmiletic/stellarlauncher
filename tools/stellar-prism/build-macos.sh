#!/bin/bash
set -euo pipefail

tool_dir="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$tool_dir/../.." && pwd)"
app="${1:-$(dirname "$root")/dist/stellar-prism/Stellar Launcher.app}"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$root/program_info/archive-launcher.icns" "$app/Contents/Resources/Stellar.icns"
ditto "$tool_dir/theme/stellar_prism" "$app/Contents/Resources/themes/stellar_prism"
ditto "$tool_dir/catpack/stellar_prism" "$app/Contents/Resources/catpacks/stellar_prism"
cp "$tool_dir/stellar-instance.png" "$app/Contents/Resources/stellar-instance.png"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
swiftc -O -framework AppKit "$tool_dir/Stellar.swift" -o "$work/Stellar"
mv -f "$work/Stellar" "$app/Contents/MacOS/Stellar"
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleExecutable</key><string>Stellar</string>
    <key>CFBundleIdentifier</key><string>app.archivelauncher.StellarPrism</string>
    <key>CFBundleName</key><string>Stellar Launcher</string>
    <key>CFBundleDisplayName</key><string>Stellar Launcher</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleIconFile</key><string>Stellar.icns</string>
    <key>CFBundleShortVersionString</key><string>1.0.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$app"
codesign --verify --strict "$app"
