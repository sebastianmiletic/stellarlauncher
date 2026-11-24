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
        baseline_sha=38d6ebd2d9ef1c940a159f0440034f61b075a1a635e44e009f6ba71a89b90699
        ;;
    macOS/x86_64)
        asset=horizon-launcher-macos-x64.zip
        sha=73fe81ba9795bef74371d6351c75828afd90c33ea2f45290db757b6bb995dace
        baseline_sha=92b1a21053e1ea613ff55be5f2de9c81f84125551ddc7e4fcf70d7540c3af2a2
        ;;
    Windows/arm64)
        asset=horizon-launcher-windows-arm64.zip
        sha=59034f4d37c1a058e4e00538d15c23a9e71a3146207a456ac1df1d5f9b0e6547
        baseline_sha=4ee858afb40edfd4ffc38c128685b8b4ab1e3ca733ba6dedc4d321146854a25d
        ;;
    Windows/x86_64)
        asset=horizon-launcher-windows-x64.zip
        sha=5189fdd6dd7c123c1284af58d1734b0e55ae4f43d8ffcac65fcc3ac71624f8bf
        baseline_sha=f9e09510e0de021c9ba0ece0b48939c48b297f66b80aefbe474e9ec35e981750
        ;;
    Linux/x86_64)
        asset=horizon-launcher-linux-x86_64.tar.gz
        sha=2253b3ed21ffb45e08d8b03007bfd646b35514016cfe59657eec9e45b66da6d7
        baseline_sha=bd2527e0cb283d4cccd3f7dec5bb51e106c93ac8318a20c8605db94a5987cd85
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
verify() {
    local checksum
    if command -v sha256sum >/dev/null; then
        checksum="$(sha256sum "$1")"
    else
        checksum="$(shasum -a 256 "$1")"
    fi
    [[ "${checksum%% *}" == "$2" ]]
}

baseline=false
if curl --fail --silent --show-error --location --retry 2 "$repository/releases/download/$tag/$asset" -o "$temp/$asset"; then
    verify "$temp/$asset" "$sha"
else
    baseline=true
    asset="StellarLauncher-1.5.11-$platform-$architecture.zip"
    if [[ "$platform" == Linux ]]; then
        asset="${asset%.zip}.AppImage"
    fi
    curl --fail --silent --show-error --location --retry 2 \
        "https://github.com/sebastianmiletic/stellarlauncher/releases/download/stellar-v1.5.11/$asset" -o "$temp/$asset"
    verify "$temp/$asset" "$baseline_sha"
fi

case "$platform" in
    macOS)
        app="$stage/ArchiveLauncher.app"
        license_dir="$app/Contents/Resources/HorizonLicenses"
        mkdir -p "$app/Contents/Helpers"
        if $baseline; then
            ditto -x -k "$temp/$asset" "$temp/unpacked"
            ditto "$temp/unpacked/Stellar Launcher.app/Contents/Helpers/Horizon.app" "$app/Contents/Helpers/Horizon.app"
        else
            ditto -x -k "$temp/$asset" "$app/Contents/Helpers"
        fi
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
        if $baseline; then
            chmod +x "$temp/$asset"
            (cd "$temp" && "./$asset" --appimage-extract 'libexec/stellar/horizon-launcher')
            mv "$temp/squashfs-root/libexec/stellar/horizon-launcher" "$temp/horizon-launcher"
        else
            tar -xzf "$temp/$asset" -C "$temp" horizon-launcher
        fi
        mkdir -p "$stage/libexec/stellar"
        install -m 755 "$temp/horizon-launcher" "$stage/libexec/stellar/horizon-launcher"
        ;;
esac

mkdir -p "$license_dir"
source="https://raw.githubusercontent.com/arismoko/horizon/$revision"
curl --fail --silent --show-error --location --retry 2 "$source/LICENSE" -o "$license_dir/LGPL-3.0.txt"
curl --fail --silent --show-error --location --retry 2 "$source/LICENSES/GPL-3.0.txt" -o "$license_dir/GPL-3.0.txt"
curl --fail --silent --show-error --location --retry 2 "$source/LICENSES/MIT.txt" -o "$license_dir/MIT.txt"
printf 'Horizon Launcher 0.2.102\nExact upstream source and build files\n%s/tree/%s\nSource archive\n%s/archive/%s.tar.gz\n' \
    "https://github.com/arismoko/horizon" "$revision" "https://github.com/arismoko/horizon" "$revision" > "$license_dir/Source.txt"
