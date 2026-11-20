#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
classpath="${1:?Pass a classpath containing Minecraft 26.3 and its Fabric Loader dependencies}"
java_home="${JAVA_HOME:?Set JAVA_HOME to a JDK 25 or newer}"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/classes"
find "$repo/tools/archive/client/src" -name '*.java' -print > "$stage/sources"
# Use individually quoted paths so builds also work from directories with spaces.
sources=()
while IFS= read -r file; do sources+=("$file"); done < "$stage/sources"
"$java_home/bin/javac" -proc:none --release 25 -classpath "$classpath" -d "$stage/classes" "${sources[@]}"
cp "$repo/tools/archive/client/resources/"*.json "$stage/classes/"
"$java_home/bin/jar" --create --file "$repo/program_info/stellar-client.jar" --date=2026-01-01T00:00:00Z -C "$stage/classes" .
