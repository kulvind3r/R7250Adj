#!/bin/bash
# R7250Adj build script
# Supports two environments:
#   MSYS2 MinGW64 (Windows) — uses native g++ from the MinGW64 toolchain
#   WSL / Linux             — uses x86_64-w64-mingw32-g++ cross-compiler
#
# Output:
#   build/R7250Adj.exe          compiled binary
#   build/R7250Adj-<version>.zip  release archive (exe + RyzenSMU.bin)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$SCRIPT_DIR/R7250Adj.cpp"
BUILD_DIR="$SCRIPT_DIR/build"
OUTPUT="$BUILD_DIR/R7250Adj.exe"
BIN_FILE="$SCRIPT_DIR/RyzenSMU.bin"

# Extract version string from source so the zip name stays in sync
VERSION=$(grep -m1 'R7250ADJ_VERSION' "$SOURCE" | grep -oP '"\K[^"]+')
RELEASE_ZIP="$BUILD_DIR/R7250Adj-${VERSION}.zip"

# ─── Environment detection ────────────────────────────────────────────────────

detect_compiler() {
    # MSYS2 MinGW64: g++ is the native Windows cross-compiler for the current arch
    if [ -n "$MSYSTEM" ] && [ "$MSYSTEM" = "MINGW64" ]; then
        if command -v g++ &>/dev/null; then
            echo "g++"
            return 0
        fi
        echo "ERROR: MSYS2 MinGW64 shell detected but g++ not found." >&2
        echo "       Run: pacman -S mingw-w64-x86_64-gcc" >&2
        return 1
    fi

    # WSL / Linux: use the cross-compiler targeting Windows x64
    if command -v x86_64-w64-mingw32-g++ &>/dev/null; then
        echo "x86_64-w64-mingw32-g++"
        return 0
    fi

    echo "ERROR: No suitable compiler found." >&2
    echo "" >&2
    echo "  On MSYS2 MinGW64:  pacman -S mingw-w64-x86_64-gcc" >&2
    echo "  On WSL / Ubuntu:   sudo apt install mingw-w64" >&2
    echo "" >&2
    echo "  Make sure you are running this script from the correct shell:" >&2
    echo "    MSYS2 -> open 'MSYS2 MinGW x64' (not plain MSYS2)" >&2
    return 1
}

# ─── Preflight checks ─────────────────────────────────────────────────────────

if [ ! -f "$SOURCE" ]; then
    echo "ERROR: Source file not found: $SOURCE" >&2
    exit 1
fi

if [ ! -f "$BIN_FILE" ]; then
    echo "ERROR: RyzenSMU.bin not found at: $BIN_FILE" >&2
    echo "       Download it from https://github.com/namazso/PawnIO.Modules/releases" >&2
    exit 1
fi

if ! command -v zip &>/dev/null; then
    echo "ERROR: 'zip' not found." >&2
    echo "  On MSYS2:         pacman -S zip" >&2
    echo "  On WSL / Ubuntu:  sudo apt install zip" >&2
    exit 1
fi

CXX=$(detect_compiler) || exit 1

mkdir -p "$BUILD_DIR"

echo "Compiler : $CXX"
echo "Source   : $SOURCE"
echo "Output   : $OUTPUT"
echo "Release  : $RELEASE_ZIP"
echo ""

# ─── Compile ──────────────────────────────────────────────────────────────────

"$CXX" \
    -O2 \
    -static \
    -o "$OUTPUT" \
    "$SOURCE" \
    -lshlwapi \
    -Wl,--subsystem,console

echo "Build complete: $OUTPUT"
echo ""

# ─── Stage bin alongside exe for local testing ────────────────────────────────

cp "$BIN_FILE" "$BUILD_DIR/RyzenSMU.bin"
echo "Staged: $BUILD_DIR/RyzenSMU.bin"

# ─── Package release zip from build directory ─────────────────────────────────

rm -f "$RELEASE_ZIP"
zip -j "$RELEASE_ZIP" "$OUTPUT" "$BUILD_DIR/RyzenSMU.bin"

echo "Release packaged: $RELEASE_ZIP"
echo ""
echo "Next steps:"
echo "  1. Run R7250Adj.exe from an elevated (Administrator) command prompt"
echo "  2. Start with: R7250Adj.exe --info"
