#!/usr/bin/env bash
set -euo pipefail

PREFIX="${HOME}/.local"
rm -f "$PREFIX/bin/eis-extractor" "$PREFIX/bin/eis-extractor-launcher"
rm -f "$PREFIX/share/applications/eis-extractor.desktop"
rm -f "$PREFIX/share/icons/hicolor/scalable/apps/eis-extractor.svg"
rm -f "$PREFIX/share/kio/servicemenus/eis-extractor-extract.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$PREFIX/share/applications" || true
fi
echo "Uninstalled E.I.S EXTRACTOR from $PREFIX"
