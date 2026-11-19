#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/eis-extractor"
if [[ ! -x "$BIN" ]]; then
    echo "Build first: cmake -S . -B build && ninja -C build" >&2
    exit 1
fi

PREFIX="${HOME}/.local"
mkdir -p "$PREFIX/bin" "$PREFIX/share/applications" "$PREFIX/share/icons/hicolor/scalable/apps" "$PREFIX/share/kio/servicemenus"

install -m 0755 "$BIN" "$PREFIX/bin/eis-extractor"
install -m 0755 "$ROOT/packaging/eis-extractor-launcher" "$PREFIX/bin/eis-extractor-launcher"
install -m 0644 "$ROOT/packaging/eis-extractor.desktop" "$PREFIX/share/applications/eis-extractor.desktop"
install -m 0644 "$ROOT/resources/icons/eis-extractor.svg" "$PREFIX/share/icons/hicolor/scalable/apps/eis-extractor.svg"
install -m 0755 "$ROOT/packaging/servicemenus/"*.desktop "$PREFIX/share/kio/servicemenus/"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$PREFIX/share/applications" || true
fi
echo "Installed E.I.S ARCHIVE to $PREFIX"
