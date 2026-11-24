#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/tools/rcd330_gui"
BUILD_DIR="$ROOT_DIR/build/gui"
RUN_TESTS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)
            RUN_TESTS=1
            shift
            ;;
        *)
            echo "Usage: $0 [--test]" >&2
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake not found. Install cmake + Qt6 (macOS: 'brew install qt cmake', Debian/Ubuntu: 'sudo apt install qt6-base-dev cmake g++')." >&2
    exit 1
fi

CMAKE_EXTRA=()
if command -v ninja >/dev/null 2>&1; then
    CMAKE_EXTRA+=(-G Ninja)
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
    QT_PREFIX=""
    if command -v brew >/dev/null 2>&1; then
        QT_PREFIX="$(brew --prefix qt 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
    fi
    if [[ -z "$QT_PREFIX" ]]; then
        for cand in /opt/homebrew/opt/qt /opt/homebrew/opt/qt@6 /usr/local/opt/qt /usr/local/opt/qt@6; do
            if [[ -d "$cand" ]]; then
                QT_PREFIX="$cand"
                break
            fi
        done
    fi
    if [[ -n "$QT_PREFIX" ]]; then
        CMAKE_EXTRA+=(-DCMAKE_PREFIX_PATH="$QT_PREFIX")
    fi
fi

mkdir -p "$BUILD_DIR"
if ! cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "${CMAKE_EXTRA[@]}"; then
    echo "Error: Qt6 configuration failed. Install Qt6 Widgets/Network (macOS: 'brew install qt cmake', Debian/Ubuntu: 'sudo apt install qt6-base-dev cmake g++')." >&2
    exit 1
fi

cmake --build "$BUILD_DIR" --parallel

if [[ "$RUN_TESTS" -eq 1 ]]; then
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
