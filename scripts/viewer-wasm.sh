#!/usr/bin/env bash
# Builds the read-only viewer for WebAssembly into <build dir>/site: viewer-wasm.sh [build dir]
# It installs Qt for WebAssembly and the emsdk that Qt version needs into EMERICK_WASM_TOOLCHAIN once. QT_HOST_PATH
# names a desktop Qt of the same version; without it the script installs one too.
set -euo pipefail

QT_VERSION=6.11.3
# Each Qt minor version is built with one emscripten version; Qt 6.11 names 4.0.7 in its qconfig.pri.
EMSDK_VERSION=4.0.7
AQT_VERSION=3.3.0

source_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-$source_dir/build/viewer-wasm}
toolchain=${EMERICK_WASM_TOOLCHAIN:-$HOME/.cache/emerick/wasm-toolchain}
jobs=${JOBS:-4}
mkdir -p "$toolchain"

# aqt writes its log into the folder it runs in.
aqt() (
    cd "$toolchain"
    if command -v uvx > /dev/null; then
        uvx --from "aqtinstall==$AQT_VERSION" aqt "$@"
    else
        command aqt "$@"
    fi
)

wasm_qt=$toolchain/qt/$QT_VERSION/wasm_singlethread
if [ ! -f "$wasm_qt/bin/qt-cmake" ]; then
    aqt install-qt all_os wasm "$QT_VERSION" wasm_singlethread --outputdir "$toolchain/qt"
fi

if [ -z "${QT_HOST_PATH:-}" ]; then
    case "$(uname -s)-$(uname -m)" in
        Darwin-*) host=(mac clang_64 macos) ;;
        Linux-aarch64) host=(linux_arm64 linux_gcc_arm64 gcc_arm64) ;;
        *) host=(linux linux_gcc_64 gcc_64) ;;
    esac
    QT_HOST_PATH=$toolchain/qt/$QT_VERSION/${host[2]}
    if [ ! -x "$QT_HOST_PATH/bin/qmake" ]; then
        aqt install-qt "${host[0]}" desktop "$QT_VERSION" "${host[1]}" --outputdir "$toolchain/qt"
    fi
fi

emsdk=$toolchain/emsdk-$EMSDK_VERSION
if [ ! -f "$emsdk/.emerick-ready" ]; then
    mkdir -p "$emsdk"
    curl -fsSL "https://github.com/emscripten-core/emsdk/archive/refs/tags/$EMSDK_VERSION.tar.gz" \
        | tar xz -C "$emsdk" --strip-components=1
    "$emsdk/emsdk" install "$EMSDK_VERSION"
    "$emsdk/emsdk" activate "$EMSDK_VERSION"
    touch "$emsdk/.emerick-ready"
fi
# shellcheck disable=SC1091
source "$emsdk/emsdk_env.sh" > /dev/null 2>&1

# The WebAssembly archives are packed on Windows, so qt-cmake lands without its executable bit.
sh "$wasm_qt/bin/qt-cmake" -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DEMERICK_VIEWER=ON -DEMERICK_BUILD_TESTS=OFF -DQT_HOST_PATH="$QT_HOST_PATH"
cmake --build "$build_dir" -j "$jobs" --target emerick_viewer

site=$build_dir/site
rm -rf "$site"
mkdir -p "$site"
cp "$build_dir"/src/viewer/{index.html,emerick_viewer.js,emerick_viewer.wasm,qtloader.js} "$site/"
for file in "$site"/*.wasm "$site"/*.js; do
    printf '%-24s %9d bytes, %9d gzip\n' "$(basename "$file")" "$(wc -c < "$file")" "$(gzip -9c "$file" | wc -c)"
done
