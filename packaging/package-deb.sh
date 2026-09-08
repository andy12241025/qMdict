#!/usr/bin/env bash
#
# Builds the native Debian package.
#
# Unlike the zip, this one bundles nothing: it links the distribution's own Qt
# and depends on it, so it has to be built on the release it is meant for.
# Ubuntu 24.04 renamed the Qt runtime packages to libqt6core6t64 and friends,
# and a package built against 22.04 will not install there, or the other way
# round.
#
# Usage: packaging/package-deb.sh [extra cmake args]

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$here/build-deb"

echo ">> configuring"
cmake -S "$here" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQMDICT_BUILD_TESTS=ON \
    "$@"

echo ">> building"
cmake --build "$build_dir" -j"$(nproc)"

echo ">> testing"
"$build_dir/qmdict_tests"

echo ">> packaging"
rm -f "$build_dir"/*.deb
cmake --build "$build_dir" --target package

mkdir -p "$here/dist"
cp "$build_dir"/*.deb "$here/dist/"

for deb in "$here"/dist/*.deb; do
    echo ">> done: dist/$(basename "$deb") ($(du -h "$deb" | cut -f1))"
    dpkg-deb -f "$deb" Depends | sed 's/^/   depends: /'
done
