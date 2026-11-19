#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
if [[ -z "$VERSION" ]]; then
    echo "Cannot read project version from CMakeLists.txt" >&2
    exit 1
fi

if ! git diff --quiet --ignore-submodules HEAD --; then
    echo "Working tree has uncommitted changes; commit them before creating a release tarball." >&2
    exit 1
fi

mkdir -p dist
NAME="eis-extractor-${VERSION}"
git archive --format=tar.gz --prefix="${NAME}/" -o "dist/${NAME}.tar.gz" HEAD
echo "Created dist/${NAME}.tar.gz"
