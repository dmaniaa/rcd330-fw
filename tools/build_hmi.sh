#!/usr/bin/env bash
# Cross-builds the recreated opt/hmi (src/hmi) into build/hmi/hmi with zig,
# against the device's own glibc 2.13 / libstdc++ 6.0.16 / libQtCore.so.4,
# then runs the ABI gate (tools/tests/test_hmi_build.py). Tables generated
# from the stock binary go to build/hmi/gen/.
# --host-tests instead builds and runs src/hmi/tests with the host compiler.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/src/hmi"
OUT_DIR="$ROOT_DIR/build/hmi"
GEN_DIR="$OUT_DIR/gen"
CMAKE_BUILD_DIR="$OUT_DIR/cmake"
THIRD_PARTY_DIR="$OUT_DIR/third_party"
DOWNLOAD_DIR="$THIRD_PARTY_DIR/downloads"

FW_ID="std_plus_pss-6.5inch_5624_VW_PQ_ROW"
SYSROOT="$ROOT_DIR/build/cache/$FW_ID/stage"
STOCK_HMI="$ROOT_DIR/extracted-fw/5624_vw/opt/hmi"
STOCK_HMI_SHA256="44ccad4211fc8f2fb21f2db46a04271155b8d894735427802baee7467630a6f2"

# GCC 4.6 libstdc++ headers matching the device libstdc++.so.6.0.16.
GCC46_URL="http://archive.debian.org/debian/pool/main/g/gcc-4.6/libstdc++6-4.6-dev_4.6.3-14_armel.deb"
GCC46_SHA256="510b4ee79ba2923d5247c9225cef0626d980e67d828ea249be4ab6a2e90a9183"
# Qt 4.7.4 sources (headers only are used); md5 matches Qt's published 9831cf1d...
QT474_URL="https://download.qt.io/archive/qt/4.7/qt-everywhere-opensource-src-4.7.4.tar.gz"
QT474_SHA256="97195ebce8a46f9929fb971d9ae58326d011c4d54425389e6e936514f540221e"
# FreeType 2.4.8 sources (headers only); the device /usr/lib/libfreetype.so.6
# is 2.4.8 (FT_New_Library stores version 2.4.8).
FT248_URL="https://download.savannah.gnu.org/releases/freetype/freetype-old/freetype-2.4.8.tar.bz2"
FT248_SHA256="a9eb7da3875fcb2f022a9c280c01b94ae45ac83d8102838c05dce1277948fb71"

FORCE=0
HOST_TESTS=0
NO_ASSETS=0
for arg in "$@"; do
    case "$arg" in
        --force) FORCE=1 ;;
        --host-tests) HOST_TESTS=1 ;;
        --no-assets) NO_ASSETS=1 ;;
        -h|--help)
            cat << 'EOF'
Usage: tools/build_hmi.sh [--force] [--host-tests [--no-assets]]

Cross-builds src/hmi into build/hmi/hmi (ARMv7 softfp, glibc 2.13, GCC 4.6
libstdc++) and runs the ABI gate. Fetches pinned third-party headers into
build/hmi/third_party/ on first use.

  --host-tests  build and run the portable unit tests in src/hmi/tests with
                the host compiler (cmake + ctest); needs no zig. Includes the
                real-asset _ovg.bin sweep, so it needs build/hmi/gen/ (from a
                full build) and the stock gresfiles (HMI_GRESFILES, default
                extracted-fw/5624_vw/opt/gresfiles); fails if either is missing
  --no-assets   with --host-tests: skip the real-asset sweep (no firmware needed)
  --force       re-extract third-party headers (cached downloads are reused when
                their sha256 matches) and rebuild from a clean CMake tree
EOF
            exit 0
            ;;
        *)
            echo "Error: unknown argument: $arg (see --help)" >&2
            exit 2
            ;;
    esac
done

# Scratch dirs created during extraction; removed on any exit, including die.
TMP_DIRS=()
cleanup_tmp_dirs() {
    if [[ ${#TMP_DIRS[@]} -gt 0 ]]; then
        rm -rf "${TMP_DIRS[@]}"
    fi
}
trap cleanup_tmp_dirs EXIT

die() {
    echo "Error: $*" >&2
    exit 1
}

sha256_of() {
    shasum -a 256 "$1" | cut -d' ' -f1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || die "'$1' not found in PATH ($2)"
}

check_local_firmware() {
    local required=(
        "$SYSROOT/lib/libc.so.6"
        "$SYSROOT/lib/libstdc++.so.6.0.16"
        "$SYSROOT/lib/libgcc_s.so.1"
        "$SYSROOT/usr/lib/libQtCore.so.4"
        "$SYSROOT/opt/lib/libvmf_client.so"
        "$SYSROOT/usr/lib/libts-1.0.so.0"
    )
    local path
    for path in "${required[@]}"; do
        [[ -e "$path" ]] || die "missing firmware sysroot file $path; run tools/build_rootfs.sh <5624 firmware .tar> to stage it"
    done
    [[ -f "$STOCK_HMI" ]] || die "missing stock binary $STOCK_HMI; extract original-fw/$FW_ID.tar into extracted-fw/5624_vw (tools/build_rootfs.sh stages the same rootfs into $SYSROOT)"
    local actual
    actual="$(sha256_of "$STOCK_HMI")"
    [[ "$actual" == "$STOCK_HMI_SHA256" ]] || die "$STOCK_HMI has sha256 $actual, expected $STOCK_HMI_SHA256 (firmware 5624); re-extract original-fw/$FW_ID.tar into extracted-fw/5624_vw"
}

# fetch <url> <sha256>: download into $DOWNLOAD_DIR once, verify, print path.
fetch() {
    local url="$1" expected="$2"
    local dest="$DOWNLOAD_DIR/$(basename "$url")"
    if [[ ! -f "$dest" || "$(sha256_of "$dest")" != "$expected" ]]; then
        echo "Downloading $url ..." >&2
        mkdir -p "$DOWNLOAD_DIR"
        curl -fsSL --show-error --retry 3 -o "$dest.part" "$url" || die "download failed: $url"
        mv -f "$dest.part" "$dest"
    fi
    local actual
    actual="$(sha256_of "$dest")"
    [[ "$actual" == "$expected" ]] || die "sha256 mismatch for $dest: got $actual, expected $expected"
    echo "$dest"
}

# is_extracted <dir> <sha256>: true when <dir> holds an extraction of that archive.
is_extracted() {
    [[ $FORCE -eq 0 && -f "$1/.stamp" && "$(cat "$1/.stamp")" == "$2" ]]
}

fetch_gcc46_headers() {
    local dest="$THIRD_PARTY_DIR/gcc46"
    is_extracted "$dest" "$GCC46_SHA256" && return 0
    local deb tmp
    deb="$(fetch "$GCC46_URL" "$GCC46_SHA256")"
    tmp="$(mktemp -d "$THIRD_PARTY_DIR/gcc46.tmp.XXXXXX")"
    TMP_DIRS+=("$tmp")
    (cd "$tmp" && ar x "$deb" data.tar.gz) || die "cannot unpack $deb with ar"
    mkdir -p "$tmp/root"
    tar -xzf "$tmp/data.tar.gz" -C "$tmp/root" ./usr/include/c++/4.6 \
        || die "cannot extract usr/include/c++/4.6 from $deb"
    # This Debian package keeps the target headers in c++/4.6/arm-linux-gnueabi.
    [[ -f "$tmp/root/usr/include/c++/4.6/arm-linux-gnueabi/bits/c++config.h" ]] \
        || die "$deb lacks usr/include/c++/4.6/arm-linux-gnueabi/bits/c++config.h"
    rm -rf "$dest"
    mkdir -p "$dest"
    mv "$tmp/root/usr/include/c++" "$dest/c++"
    rm -rf "$tmp"
    echo "$GCC46_SHA256" > "$dest/.stamp"
}

fetch_qt474_headers() {
    local dest="$THIRD_PARTY_DIR/qt474"
    is_extracted "$dest" "$QT474_SHA256" && return 0
    local tarball tmp top="qt-everywhere-opensource-src-4.7.4"
    tarball="$(fetch "$QT474_URL" "$QT474_SHA256")"
    tmp="$(mktemp -d "$THIRD_PARTY_DIR/qt474.tmp.XXXXXX")"
    TMP_DIRS+=("$tmp")
    echo "Extracting Qt 4.7.4 QtCore headers ..." >&2
    tar -xzf "$tarball" -C "$tmp" "$top/include/QtCore" "$top/src/corelib" \
        || die "cannot extract QtCore headers from $tarball"
    # include/QtCore/*.h forward to src/corelib/**; keep only headers there.
    find "$tmp/$top/src/corelib" -type f ! -name '*.h' -delete
    rm -rf "$dest"
    mkdir -p "$dest/src"
    mv "$tmp/$top/include" "$dest/include"
    mv "$tmp/$top/src/corelib" "$dest/src/corelib"
    rm -rf "$tmp"
    echo "$QT474_SHA256" > "$dest/.stamp"
}

fetch_freetype248_headers() {
    local dest="$THIRD_PARTY_DIR/freetype248"
    is_extracted "$dest" "$FT248_SHA256" && return 0
    local tarball tmp top="freetype-2.4.8"
    tarball="$(fetch "$FT248_URL" "$FT248_SHA256")"
    tmp="$(mktemp -d "$THIRD_PARTY_DIR/freetype248.tmp.XXXXXX")"
    TMP_DIRS+=("$tmp")
    tar -xjf "$tarball" -C "$tmp" "$top/include" || die "cannot extract FreeType headers from $tarball"
    [[ -f "$tmp/$top/include/ft2build.h" ]] || die "$tarball lacks include/ft2build.h"
    rm -rf "$dest"
    mkdir -p "$dest"
    mv "$tmp/$top/include" "$dest/include"
    rm -rf "$tmp"
    echo "$FT248_SHA256" > "$dest/.stamp"
}

build_host_moc() {
    local moc_src="$THIRD_PARTY_DIR/qt474_moc_src"
    local moc_build="$OUT_DIR/host-tools/build"
    local moc_bin="$OUT_DIR/host-tools/moc"
    if ! is_extracted "$moc_src" "$QT474_SHA256"; then
        local tarball tmp top="qt-everywhere-opensource-src-4.7.4"
        tarball="$(fetch "$QT474_URL" "$QT474_SHA256")"
        tmp="$(mktemp -d "$THIRD_PARTY_DIR/qt474_moc.tmp.XXXXXX")"
        TMP_DIRS+=("$tmp")
        echo "Extracting Qt 4.7.4 moc + bootstrap sources ..." >&2
        tar -xzf "$tarball" -C "$tmp" \
            "$top/src/tools/moc" \
            "$top/src/corelib" \
            "$top/include/QtCore" \
            "$top/mkspecs/common" \
            "$top/mkspecs/macx-g++" \
            "$top/mkspecs/linux-g++" \
            || die "cannot extract moc sources from $tarball"
        rm -rf "$moc_src"
        mv "$tmp/$top" "$moc_src"
        rm -rf "$tmp"
        echo "$QT474_SHA256" > "$moc_src/.stamp"
    fi
    if [[ $FORCE -eq 0 && -x "$moc_bin" && "$moc_bin" -nt "$SRC_DIR/tools/moc/CMakeLists.txt" \
          && "$moc_bin" -nt "$SRC_DIR/tools/moc/host_qconfig.h" && "$moc_bin" -nt "$moc_src/.stamp" ]]; then
        return 0
    fi
    if [[ $FORCE -eq 1 ]]; then
        rm -rf "$moc_build"
    fi
    mkdir -p "$OUT_DIR/host-tools"
    echo "Building host Qt 4.7.4 moc ..." >&2
    cmake -S "$SRC_DIR/tools/moc" -B "$moc_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DRCD330_QT474_SRC_DIR="$moc_src" \
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$OUT_DIR/host-tools"
    cmake --build "$moc_build"
}

[[ $NO_ASSETS -eq 0 || $HOST_TESTS -eq 1 ]] || die "--no-assets only applies to --host-tests"

if [[ $HOST_TESTS -eq 1 ]]; then
    require_tool cmake "install cmake"
    HOST_TEST_DIR="$OUT_DIR/host-tests"
    HOST_GEN_DIR=""
    if [[ $NO_ASSETS -eq 0 ]]; then
        # The real-asset sweep needs the generated tex_table and the stock
        # gresfiles; refuse to silently skip it.
        [[ -f "$GEN_DIR/igdt_tables_vw.cpp" ]] \
            || die "missing $GEN_DIR/igdt_tables_vw.cpp; run tools/build_hmi.sh first (or pass --no-assets)"
        HMI_GRESFILES="${HMI_GRESFILES:-$ROOT_DIR/extracted-fw/5624_vw/opt/gresfiles}"
        [[ -d "$HMI_GRESFILES" ]] \
            || die "missing gresfiles directory $HMI_GRESFILES; extract original-fw/$FW_ID.tar into extracted-fw/5624_vw or set HMI_GRESFILES (or pass --no-assets)"
        # ctest runs from the build dir, so a relative path must be made absolute.
        HMI_GRESFILES="$(cd "$HMI_GRESFILES" && pwd)"
        export HMI_GRESFILES
        HOST_GEN_DIR="$GEN_DIR"
    fi
    if [[ $FORCE -eq 1 ]]; then
        rm -rf "$HOST_TEST_DIR"
    fi
    # An empty RCD330_GEN_DIR compiles the real-asset sweep out (--no-assets).
    cmake -S "$SRC_DIR/tests" -B "$HOST_TEST_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DRCD330_GEN_DIR="$HOST_GEN_DIR"
    cmake --build "$HOST_TEST_DIR"
    ctest --test-dir "$HOST_TEST_DIR" --output-on-failure
    exit 0
fi

require_tool zig "install zig (brew install zig)"
require_tool cmake "install cmake"
require_tool ninja "install ninja"
require_tool curl "needed to fetch third-party headers"
require_tool ar "needed to unpack the GCC 4.6 .deb"
require_tool tar "needed to extract third-party headers"
require_tool shasum "needed to verify downloads"
require_tool python3 "needed for the ABI gate"

check_local_firmware

echo "Generating IGDT tables ..."
python3 "$SRC_DIR/tools/gen_igdt_tables.py" --hmi "$STOCK_HMI" --out "$GEN_DIR/igdt_tables_vw.cpp" \
    || die "IGDT table generation failed (src/hmi/tools/gen_igdt_tables.py)"

echo "Generating string tables ..."
python3 "$SRC_DIR/tools/gen_strings.py" --hmi "$STOCK_HMI" --out "$GEN_DIR/strings_vw.cpp" \
    || die "string table generation failed (src/hmi/tools/gen_strings.py)"

mkdir -p "$THIRD_PARTY_DIR"
fetch_gcc46_headers
fetch_qt474_headers
fetch_freetype248_headers
build_host_moc

if [[ $FORCE -eq 1 ]]; then
    rm -rf "$CMAKE_BUILD_DIR"
fi
cmake -S "$SRC_DIR" -B "$CMAKE_BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SRC_DIR/cmake/zig-armv7-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DRCD330_SYSROOT="$SYSROOT" \
    -DRCD330_GCC46_DIR="$THIRD_PARTY_DIR/gcc46" \
    -DRCD330_QT474_DIR="$THIRD_PARTY_DIR/qt474" \
    -DRCD330_FT248_DIR="$THIRD_PARTY_DIR/freetype248" \
    -DRCD330_MOC_EXECUTABLE="$OUT_DIR/host-tools/moc" \
    -DRCD330_GEN_DIR="$GEN_DIR"
cmake --build "$CMAKE_BUILD_DIR" --target hmi
# Editors/clangd look for compile_commands.json next to the sources
# (gitignored via src/hmi/.gitignore); without it includes like
# display/igdt/... show as "file not found".
ln -sf "$CMAKE_BUILD_DIR/compile_commands.json" "$SRC_DIR/compile_commands.json"

# Gate a staging copy first: build_rootfs.sh --hmi and run_rcd330.sh pick up
# build/hmi/hmi, so a binary that fails the gate must never land there.
STAGED_HMI="$OUT_DIR/hmi.new"
cp -f "$CMAKE_BUILD_DIR/hmi" "$STAGED_HMI"
echo "Running ABI gate on $STAGED_HMI ..."
if ! (cd "$ROOT_DIR" && RCD330_REQUIRE_HMI=1 RCD330_HMI="$STAGED_HMI" \
        python3 -m unittest tools/tests/test_hmi_build.py); then
    rm -f "$STAGED_HMI"
    die "ABI gate failed; $OUT_DIR/hmi left unchanged (see tools/tests/test_hmi_build.py)"
fi
mv -f "$STAGED_HMI" "$OUT_DIR/hmi"
echo "Built $OUT_DIR/hmi"
