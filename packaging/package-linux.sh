#!/usr/bin/env bash
#
# Builds qMdict and bundles it with the Qt libraries it needs into a zip that
# runs on any reasonably modern desktop Linux without installing anything.
#
# Usage: packaging/package-linux.sh [/path/to/Qt/6.x.y/gcc_64]

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qt_prefix="${1:-${QT_PREFIX:-}}"
build_dir="$here/build-package"
version="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$here/CMakeLists.txt" | head -1)"
name="qMdict-${version}-linux-x86_64"
stage="$here/dist/$name"

if [[ -z "$qt_prefix" ]]; then
    echo "error: pass the Qt prefix, e.g. packaging/package-linux.sh ~/Qt/6.8.3/gcc_64" >&2
    exit 1
fi

echo ">> configuring"
cmake -S "$here" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQMDICT_BUILD_TESTS=ON \
    -DCMAKE_PREFIX_PATH="$qt_prefix${CMAKE_EXTRA_PREFIX:+;$CMAKE_EXTRA_PREFIX}" \
    "${@:2}"

echo ">> building"
cmake --build "$build_dir" -j"$(nproc)"

echo ">> testing"
"$build_dir/qmdict_tests"

echo ">> staging into $stage"
rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/lib" "$stage/plugins" "$stage/data"
cp "$build_dir/qMdict" "$stage/bin/"
strip --strip-unneeded "$stage/bin/qMdict" 2>/dev/null || true

# Point Qt at the bundled plugins rather than the build machine's.
cat > "$stage/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Libraries = lib
EOF

# Only the plugin families a widgets app actually loads.
for group in platforminputcontexts imageformats iconengines styles; do
    if [[ -d "$qt_prefix/plugins/$group" ]]; then
        mkdir -p "$stage/plugins/$group"
        cp -a "$qt_prefix/plugins/$group/." "$stage/plugins/$group/"
    fi
done

# Desktop X11 plus two headless fallbacks. The embedded backends (eglfs,
# linuxfb, vnc) would drag in half of Qt's OpenGL and networking stack.
mkdir -p "$stage/plugins/platforms"
for plugin in libqxcb.so libqminimal.so libqoffscreen.so; do
    [[ -f "$qt_prefix/plugins/platforms/$plugin" ]] &&
        cp "$qt_prefix/plugins/platforms/$plugin" "$stage/plugins/platforms/"
done

# Copy every shared library the binary and plugins resolve to inside the Qt
# prefix. System libraries (glibc, X11, OpenGL) are deliberately left out:
# bundling them is what breaks portability, not what fixes it.
collect_libs() {
    local target="$1"
    LD_LIBRARY_PATH="$qt_prefix/lib:${LD_LIBRARY_PATH:-}" ldd "$target" 2>/dev/null |
        awk '/=> \//{print $3}' |
        while read -r lib; do
            case "$lib" in
                "$qt_prefix"/*|*/libicu*|*/libQt6*)
                    local base
                    base="$(basename "$lib")"
                    [[ -e "$stage/lib/$base" ]] && continue
                    cp -L "$lib" "$stage/lib/$base"
                    collect_libs "$lib"
                    ;;
            esac
        done
}

collect_libs "$stage/bin/qMdict"
while read -r plugin; do
    collect_libs "$plugin"
done < <(find "$stage/plugins" -name '*.so')

strip --strip-unneeded "$stage"/lib/*.so* 2>/dev/null || true

cat > "$stage/qMdict" <<'EOF'
#!/usr/bin/env bash
# Launches qMdict in portable mode: settings and the headword index cache are
# written to the data/ folder beside this script, not to your home directory.
here="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
exec "$here/bin/qMdict" --portable "$@"
EOF
chmod +x "$stage/qMdict"

cat > "$stage/README.txt" <<EOF
qMdict $version - offline MDict (.mdx/.mdd) dictionary reader

Run ./qMdict, then choose File > Open Dictionary Folder and pick the folder
holding your dictionaries. Sub-folders are scanned recursively.

Everything needed is inside this directory; nothing is installed system-wide.
Settings and the headword index cache live in ./data and can be deleted safely.
EOF

echo ">> zipping"
mkdir -p "$here/dist"
(cd "$here/dist" && rm -f "$name.zip" && zip -qr9 "$name.zip" "$name")

echo ">> done: dist/$name.zip ($(du -h "$here/dist/$name.zip" | cut -f1))"
