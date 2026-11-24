#!/bin/sh
# Wrapper around Ghidra analyzeHeadless for the RH850 VIP image.
# Keeps Ghidra user settings inside build/ghidra/home so it runs without touching ~/Library.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
GHIDRA=${GHIDRA_INSTALL_DIR:-/opt/homebrew/opt/ghidra/libexec}
mkdir -p "$ROOT/build/ghidra/home"
export JAVA_HOME=${JAVA_HOME:-$(/usr/libexec/java_home 2>/dev/null || echo /opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home)}
export JAVA_TOOL_OPTIONS="-Duser.home=$ROOT/build/ghidra/home"
exec "$GHIDRA/support/analyzeHeadless" "$ROOT/build/ghidra" "$@"
