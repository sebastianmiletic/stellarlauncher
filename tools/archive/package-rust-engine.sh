#!/usr/bin/env bash
set -euo pipefail

platform="$1"
architecture="$2"
stage="$3"
tag='launcher%400.2.102'
revision=65686f40f394cbd80771b18cd92d911543c9c967
repository='https://github.com/horizon-rust/horizon'
license_dir="$stage/share/licenses/stellar-horizon"

case "$platform/$architecture" in
    macOS/arm64)
        asset=horizon-launcher-macos-arm64.zip
        sha=5938c5613d114151a03d42e2537c2d3f9378df0c82f62150627cd0d056f87a0d
        ;;
    macOS/x86_64)
        asset=horizon-launcher-macos-x64.zip
        sha=73fe81ba9795bef74371d6351c75828afd90c33ea2f45290db757b6bb995dace
        ;;
    Windows/arm64)
        asset=horizon-launcher-windows-arm64.zip
        sha=59034f4d37c1a058e4e00538d15c23a9e71a3146207a456ac1df1d5f9b0e6547
        ;;
    Windows/x86_64)
        asset=horizon-launcher-windows-x64.zip
        sha=5189fdd6dd7c123c1284af58d1734b0e55ae4f43d8ffcac65fcc3ac71624f8bf
        ;;
    Linux/x86_64)
        asset=horizon-launcher-linux-x86_64.tar.gz
        sha=2253b3ed21ffb45e08d8b03007bfd646b35514016cfe59657eec9e45b66da6d7
        ;;
    Linux/aarch64)
        printf 'The upstream Rust runtime does not provide Linux ARM64 builds.\n'
        exit 0
        ;;
    *)
        printf 'Unsupported Rust packaging target %s/%s\n' "$platform" "$architecture" >&2
        exit 1
        ;;
esac

temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT
curl --fail --silent --show-error --location --retry 2 "$repository/releases/download/$tag/$asset" -o "$temp/$asset"
if command -v sha256sum >/dev/null; then
    checksum="$(sha256sum "$temp/$asset")"
else
    checksum="$(shasum -a 256 "$temp/$asset")"
fi
if [[ "${checksum%% *}" != "$sha" ]]; then
    printf 'Unexpected checksum for %s\n' "$asset" >&2
    exit 1
fi

case "$platform" in
    macOS)
        app="$stage/ArchiveLauncher.app"
        license_dir="$app/Contents/Resources/HorizonLicenses"
        mkdir -p "$app/Contents/Helpers"
        ditto -x -k "$temp/$asset" "$app/Contents/Helpers"
        codesign --verify --strict "$app/Contents/Helpers/Horizon.app"
        ;;
    Windows)
        license_dir="$stage/HorizonLicenses"
        unzip -q "$temp/$asset" -d "$temp/unpacked"
        executable="$(find "$temp/unpacked" -type f -name horizon-launcher.exe -print -quit)"
        test -n "$executable"
        install -m 755 "$executable" "$stage/horizon-launcher.exe"
        ;;
    Linux)
        tar -xzf "$temp/$asset" -C "$temp" horizon-launcher
        mkdir -p "$stage/libexec/stellar"
        install -m 755 "$temp/horizon-launcher" "$stage/libexec/stellar/horizon-launcher"
        ;;
esac

mkdir -p "$license_dir"
source="https://raw.githubusercontent.com/horizon-rust/horizon/$revision"
curl --fail --silent --show-error --location --retry 2 "$source/LICENSE" -o "$license_dir/LGPL-3.0.txt"
curl --fail --silent --show-error --location --retry 2 "$source/LICENSES/GPL-3.0.txt" -o "$license_dir/GPL-3.0.txt"
curl --fail --silent --show-error --location --retry 2 "$source/LICENSES/MIT.txt" -o "$license_dir/MIT.txt"
printf 'Horizon Launcher 0.2.102\nExact upstream source and build files\n%s/tree/%s\nSource archive\n%s/archive/%s.tar.gz\n' \
    "$repository" "$revision" "$repository" "$revision" > "$license_dir/Source.txt"
