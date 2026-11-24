#!/usr/bin/env bash
set -euo pipefail

version="$(sed -nE 's/^set\(Launcher_VERSION_(MAJOR|MINOR|PATCH) ([0-9]+)\)$/\2/p' CMakeLists.txt | paste -sd . -)"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
stage="$PWD/stage"
downloads="$PWD/downloads"
product="StellarLauncher-$version-$STELLAR_PLATFORM-$STELLAR_ARCH"
mkdir -p "$downloads"

case "$STELLAR_PLATFORM" in
    macOS)
        codesign --force --deep --sign - "$stage/ArchiveLauncher.app"
        bash tools/archive/package-rust-engine.sh "$STELLAR_PLATFORM" "$STELLAR_ARCH" "$stage"
        mv "$stage/ArchiveLauncher.app" "$stage/Stellar Launcher.app"
        app="$stage/Stellar Launcher.app"
        codesign --force --sign - "$app"
        codesign --verify --deep --strict "$app"
        "$app/Contents/MacOS/ArchiveLauncher" --version
        ditto -c -k --keepParent "$app" "$downloads/$product.zip"
        ;;
    Windows)
        bash tools/archive/package-rust-engine.sh "$STELLAR_PLATFORM" "$STELLAR_ARCH" "$stage"
        cmake --install build --component portable
        STELLAR_ARCHIVE="$downloads/$product.zip" pwsh -NoProfile -Command \
            'Compress-Archive -Path stage/* -DestinationPath $env:STELLAR_ARCHIVE'
        ;;
    Linux)
        bash tools/archive/package-rust-engine.sh "$STELLAR_PLATFORM" "$STELLAR_ARCH" "$stage"
        executable="$stage/bin/archivelauncher"
        "$executable" --version
        desktop="app.archivelauncher.ArchiveLauncher"
        tooling="$(mktemp -d)"
        gh release download --repo VHSgunzo/sharun --pattern "sharun-$STELLAR_ARCH-aio" --output "$tooling/sharun"
        gh release download continuous --repo DioEgizio/go-appimage --pattern "mkappimage-*-$STELLAR_ARCH.AppImage" \
            --output "$tooling/mkappimage"
        chmod +x "$tooling/sharun" "$tooling/mkappimage"
        "$tooling/sharun" lib4bin --hard-links --with-hooks --dst-dir "$stage" "$executable" "$stage/bin/archivelauncher_updater" \
            "$QT_PLUGIN_PATH/"*/*.so
        mv "$stage/sharun" "$stage/AppRun"
        printf '%s\n' 'ARCHIVELAUNCHER_DISABLE_GLVULKAN=1' > "$stage/.env"
        ln -s "share/applications/$desktop.desktop" "$stage/$desktop.desktop"
        ln -s "share/icons/hicolor/256x256/apps/$desktop.png" "$stage/$desktop.png"
        ln -s "$desktop.png" "$stage/.DirIcon"
        APPIMAGE_EXTRACT_AND_RUN=1 "$tooling/mkappimage" "$stage" "$downloads/$product.AppImage"
        ;;
    *)
        printf 'Unsupported platform %s\n' "$STELLAR_PLATFORM" >&2
        exit 1
        ;;
esac

(
    cd "$downloads"
    for package in "$product".*; do
        if command -v sha256sum >/dev/null; then
            sha256sum "$package" > "$package.sha256"
        else
            shasum -a 256 "$package" > "$package.sha256"
        fi
    done
)
