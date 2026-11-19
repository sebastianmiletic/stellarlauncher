#!/bin/bash
set -euo pipefail
source_dir="$(cd "$(dirname "$0")/../.." && pwd)"
workspace_dir="$(dirname "$source_dir")"
export PKG_CONFIG_PATH="/opt/homebrew/opt/libarchive/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
cmake -S "$source_dir" -B "$workspace_dir/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO=ON -DLauncher_USE_PCH=OFF \
  -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_INSTALL_PREFIX="$workspace_dir/dist" \
  -DLauncher_BUILD_PLATFORM=archive-macos-arm64 -DBUILD_TESTING=ON \
  -DJava_JAVA_EXECUTABLE=/opt/homebrew/opt/openjdk@17/bin/java \
  -DJava_JAVAC_EXECUTABLE=/opt/homebrew/opt/openjdk@17/bin/javac \
  -DMACOSX_SPARKLE_UPDATE_FEED_URL="${ARCHIVE_UPDATE_FEED_URL:-}" \
  -DMACOSX_SPARKLE_UPDATE_PUBLIC_KEY="${ARCHIVE_UPDATE_PUBLIC_KEY:-}" \
  -DLauncher_MSA_CLIENT_ID="${ARCHIVE_MSA_CLIENT_ID:-}" \
  -DLauncher_CURSEFORGE_API_KEY="${ARCHIVE_CURSEFORGE_API_KEY:-}"
cmake --build "$workspace_dir/build" -j "${ARCHIVE_BUILD_JOBS:-6}"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$workspace_dir/build" --output-on-failure
mkdir -p "$workspace_dir/dist"
stage="$(mktemp -d "$workspace_dir/dist/.stellar-build.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
cmake --install "$workspace_dir/build" --prefix "$stage"
app="$stage/ArchiveLauncher.app"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
"$app/Contents/MacOS/ArchiveLauncher" --version

stellar="$workspace_dir/dist/Stellar Launcher.app"
previous="$stage/previous.app"
if [[ -d "$stellar" ]]; then
  mv "$stellar" "$previous"
fi
if ! mv "$app" "$stellar"; then
  if [[ -d "$previous" ]]; then
    mv "$previous" "$stellar"
  fi
  exit 1
fi
