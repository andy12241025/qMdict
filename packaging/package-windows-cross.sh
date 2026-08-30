#!/usr/bin/env bash
#
# Cross-builds the Windows zip from Linux using MinGW-w64 and Qt's official
# win64_mingw package. Produces the same layout as package-windows.ps1, so
# either script can be used to cut a release.
#
# Usage:
#   packaging/package-windows-cross.sh \
#       --qt-windows /path/to/Qt/6.8.3/mingw_64 \
#       --qt-host    /path/to/Qt/6.8.3/gcc_64 \
#       [--mingw-root /prefix/containing/usr/bin/x86_64-w64-mingw32-g++]

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qt_windows=""
qt_host=""
mingw_root=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --qt-windows) qt_windows="$2"; shift 2 ;;
        --qt-host)    qt_host="$2";    shift 2 ;;
        --mingw-root) mingw_root="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$qt_windows" || -z "$qt_host" ]]; then
    echo "error: --qt-windows and --qt-host are both required" >&2
    exit 1
fi

build_dir="$here/build-windows"
version="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$here/CMakeLists.txt" | head -1)"
name="qMdict-${version}-windows-x86_64"
stage="$here/dist/$name"

objdump="x86_64-w64-mingw32-objdump"
strip_tool="x86_64-w64-mingw32-strip"
[[ -n "$mingw_root" ]] && objdump="$mingw_root/usr/bin/$objdump"
[[ -n "$mingw_root" ]] && strip_tool="$mingw_root/usr/bin/$strip_tool"

echo ">> configuring"
cmake -S "$here" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQMDICT_BUILD_TESTS=ON \
    -DCMAKE_TOOLCHAIN_FILE="$here/packaging/toolchain-mingw.cmake" \
    ${mingw_root:+-DMINGW_ROOT="$mingw_root"} \
    -DCMAKE_PREFIX_PATH="$qt_windows" \
    -DQT_HOST_PATH="$qt_host"

echo ">> building"
cmake --build "$build_dir" -j"$(nproc)"

echo ">> staging into $stage"
rm -rf "$stage"
mkdir -p "$stage" "$stage/data"
cp "$build_dir/qMdict.exe" "$stage/"
"$strip_tool" --strip-unneeded "$stage/qMdict.exe" 2>/dev/null || true

# Qt finds plugins in <appdir>/<plugin type>/ on Windows, so they go beside
# the executable rather than under a plugins/ folder.
mkdir -p "$stage/platforms" "$stage/styles" "$stage/imageformats"
cp "$qt_windows/plugins/platforms/qwindows.dll" "$stage/platforms/"
cp "$qt_windows"/plugins/styles/*.dll "$stage/styles/" 2>/dev/null || true
cp "$qt_windows"/plugins/imageformats/*.dll "$stage/imageformats/" 2>/dev/null || true

# Walk the import tables and copy every non-system DLL we can find, which is
# the cross-compiling equivalent of what windeployqt does.
search_dirs=("$qt_windows/bin")
if [[ -n "$mingw_root" ]]; then
    while IFS= read -r d; do search_dirs+=("$d"); done < <(
        find "$mingw_root" -name 'lib*.dll' -printf '%h\n' | sort -u)
fi

find_dll() {
    local wanted="$1"
    for d in "${search_dirs[@]}"; do
        if [[ -f "$d/$wanted" ]]; then
            echo "$d/$wanted"
            return 0
        fi
    done
    return 1
}

collect_dlls() {
    local target="$1"
    while IFS= read -r dll; do
        [[ -e "$stage/$dll" ]] && continue
        local found
        if found="$(find_dll "$dll")"; then
            cp "$found" "$stage/$dll"
            "$strip_tool" --strip-unneeded "$stage/$dll" 2>/dev/null || true
            collect_dlls "$stage/$dll"
        fi
    done < <("$objdump" -p "$target" | awk '/DLL Name:/{print $3}')
}

collect_dlls "$stage/qMdict.exe"
for plugin in "$stage"/platforms/*.dll "$stage"/styles/*.dll "$stage"/imageformats/*.dll; do
    [[ -f "$plugin" ]] && collect_dlls "$plugin"
done

# A cross-build cannot be launched here, so verify statically that every
# imported DLL is either bundled or provided by Windows itself. A missing entry
# means the app would fail to start with no useful error.
echo ">> checking imports"
system_dlls='^(ntdll|kernel32|kernelbase|user32|gdi32|gdiplus|shell32|shcore|shlwapi|advapi32|ole32|oleaut32|oleacc|comdlg32|comctl32|ws2_32|wsock32|iphlpapi|msvcrt|ucrtbase|msvcp[0-9_]*|vcruntime[0-9_]*|winspool|imm32|winmm|version|netapi32|userenv|secur32|crypt32|bcrypt|ncrypt|wtsapi32|dwmapi|dwrite|d2d1|windowscodecs|uxtheme|setupapi|cfgmgr32|mpr|rpcrt4|d3d9|d3d11|d3d12|dxgi|dxva2|opengl32|glu32|authz|mswsock|dnsapi|winhttp|wininet|urlmon|powrprof|propsys|dbghelp|psapi|wintrust|usp10|normaliz|hid|cabinet|api-ms-win-.*|ext-ms-.*)\.dll$'

missing=0
while IFS= read -r pe; do
    while IFS= read -r dll; do
        lower="$(echo "$dll" | tr 'A-Z' 'a-z')"
        [[ "$lower" =~ $system_dlls ]] && continue
        if ! find "$stage" -iname "$dll" -print -quit | grep -q .; then
            echo "   MISSING: $dll (needed by ${pe#$stage/})"
            missing=1
        fi
    done < <("$objdump" -p "$pe" | awk '/DLL Name:/{print $3}')
done < <(find "$stage" -name '*.dll' -o -name '*.exe')

if [[ $missing -ne 0 ]]; then
    echo "error: the bundle is incomplete" >&2
    exit 1
fi
echo "   all imports resolve"

cat > "$stage/qMdict-portable.cmd" <<'EOF'
@echo off
start "" "%~dp0qMdict.exe" --portable %*
EOF
unix2dos -q "$stage/qMdict-portable.cmd" 2>/dev/null || true

cat > "$stage/README.txt" <<EOF
qMdict $version - offline MDict (.mdx/.mdd) dictionary reader

Run qMdict.exe, then choose File > Open Dictionary Folder and pick the folder
holding your dictionaries. Sub-folders are scanned recursively.

Everything needed is inside this directory; nothing is installed system-wide.
Start qMdict-portable.cmd instead to keep settings and the headword index
cache in the data\\ folder here rather than in your Windows user profile.
EOF
unix2dos -q "$stage/README.txt" 2>/dev/null || true

echo ">> zipping"
mkdir -p "$here/dist"
(cd "$here/dist" && rm -f "$name.zip" && zip -qr9 "$name.zip" "$name")

echo ">> done: dist/$name.zip ($(du -h "$here/dist/$name.zip" | cut -f1))"
echo "   contents:"
(cd "$stage" && find . -type f | sort | sed 's/^/     /')
