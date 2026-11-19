#!/bin/bash
set -euo pipefail

tool_dir="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$tool_dir/../.." && pwd)"
app="${1:-$(dirname "$root")/dist/stellar-prism/Stellar Launcher.app}"
engine="${2:-$(dirname "$root")/dist/Stellar Launcher.app}"
if [[ ! -x "$engine/Contents/MacOS/ArchiveLauncher" ]]; then
    echo "Build the standalone Stellar engine with tools/archive/build-macos.sh first." >&2
    exit 1
fi
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$tool_dir/wallpapers.json" "$app/Contents/Resources/wallpapers.json"
cp "$root/program_info/stellar-wallpaper.jpg" "$app/Contents/Resources/stellar-wallpaper.jpg"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
rsvg-convert "$tool_dir/stellar-mark.svg" -w 1024 -h 1024 -o "$work/mark.png"
mkdir -p "$work/Stellar.iconset"
for size in 16 32 128 256 512; do
    sips -s format png -z "$size" "$size" "$work/mark.png" --out "$work/Stellar.iconset/icon_${size}x${size}.png" >/dev/null
    sips -s format png -z "$((size * 2))" "$((size * 2))" "$work/mark.png" --out "$work/Stellar.iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$work/Stellar.iconset" -o "$app/Contents/Resources/Stellar.icns"
sips -s format png -z 256 256 "$work/mark.png" --out "$app/Contents/Resources/stellar-instance.png" >/dev/null
swiftc -O -target arm64-apple-macos14.0 -framework AppKit -framework SwiftUI "$tool_dir"/*.swift -o "$work/Stellar"
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
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>CFBundleShortVersionString</key><string>1.3.0</string>
    <key>CFBundleVersion</key><string>5</string>
    <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
helper="$app/Contents/Helpers/Stellar Engine.app"
mkdir -p "$app/Contents/Helpers"
ditto "$engine" "$helper"
cp "$app/Contents/Resources/Stellar.icns" "$helper/Contents/Resources/Stellar.icns"
helper_plist="$helper/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile Stellar.icns" "$helper_plist"
/usr/libexec/PlistBuddy -c "Delete :SUFeedURL" "$helper_plist" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Delete :SUPublicEDKey" "$helper_plist" 2>/dev/null || true
codesign --force --sign - "$helper"
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"
