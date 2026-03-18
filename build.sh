#!/bin/bash
# R7250Adj build script
# Supports two environments:
#   MSYS2 MinGW64 (Windows) — uses native g++ from the MinGW64 toolchain
#   WSL / Linux             — uses x86_64-w64-mingw32-g++ cross-compiler
#
# Output:
#   build/R7250Adj.exe            compiled binary (with embedded icon)
#   build/R7250Adj-<version>.zip  release archive (exe + RyzenSMU.bin)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$SCRIPT_DIR/R7250Adj.cpp"
BUILD_DIR="$SCRIPT_DIR/build"
OUTPUT="$BUILD_DIR/R7250Adj.exe"
BIN_FILE="$SCRIPT_DIR/RyzenSMU.bin"
ICON_FILE="$SCRIPT_DIR/icon.ico"
RESOURCE_RC="$SCRIPT_DIR/resource.rc"
RESOURCE_OBJ="$SCRIPT_DIR/resource.o"

# Extract version string from source so the zip name and version resource stay in sync
VERSION=$(grep -m1 'R7250ADJ_VERSION' "$SOURCE" | grep -oP '"\K[^"]+')
RELEASE_ZIP="$BUILD_DIR/R7250Adj-v${VERSION}.zip"

# Split version into components for the VERSIONINFO resource (requires numeric fields)
IFS='.' read -r VER_MAJOR VER_MINOR VER_PATCH <<< "$VERSION"
VER_MAJOR=${VER_MAJOR:-1}
VER_MINOR=${VER_MINOR:-0}
VER_PATCH=${VER_PATCH:-0}

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

detect_windres() {
    if [ -n "$MSYSTEM" ] && [ "$MSYSTEM" = "MINGW64" ]; then
        if command -v windres &>/dev/null; then
            echo "windres"
            return 0
        fi
        echo "ERROR: windres not found in MSYS2 MinGW64." >&2
        echo "       Run: pacman -S mingw-w64-x86_64-binutils" >&2
        return 1
    fi

    if command -v x86_64-w64-mingw32-windres &>/dev/null; then
        echo "x86_64-w64-mingw32-windres"
        return 0
    fi

    echo "ERROR: windres not found (needed for icon embedding)." >&2
    echo "  On MSYS2 MinGW64:  pacman -S mingw-w64-x86_64-binutils" >&2
    echo "  On WSL / Ubuntu:   sudo apt install mingw-w64" >&2
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

if [ ! -f "$ICON_FILE" ]; then
    echo "ERROR: Icon file not found: $ICON_FILE" >&2
    exit 1
fi

if ! command -v zip &>/dev/null; then
    echo "ERROR: 'zip' not found." >&2
    echo "  On MSYS2:         pacman -S zip" >&2
    echo "  On WSL / Ubuntu:  sudo apt install zip" >&2
    exit 1
fi

CXX=$(detect_compiler) || exit 1
WINDRES=$(detect_windres) || exit 1

mkdir -p "$BUILD_DIR"

echo "Compiler : $CXX"
echo "Windres  : $WINDRES"
echo "Source   : $SOURCE"
echo "Output   : $OUTPUT"
echo "Release  : $RELEASE_ZIP"
echo ""

# ─── Compile resources (icon + version info) ─────────────────────────────────
# Both are combined into one .rc so a single windres call handles both.
# Run windres from SCRIPT_DIR so "icon.ico" resolves without path translation issues.

cat > "$RESOURCE_RC" << EOF
#include <windows.h>

1 ICON "icon.ico"

VS_VERSION_INFO VERSIONINFO
FILEVERSION    ${VER_MAJOR},${VER_MINOR},${VER_PATCH},0
PRODUCTVERSION ${VER_MAJOR},${VER_MINOR},${VER_PATCH},0
FILEFLAGSMASK  VS_FFI_FILEFLAGSMASK
FILEFLAGS      0x0L
FILEOS         VOS_NT_WINDOWS32
FILETYPE       VFT_APP
FILESUBTYPE    VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName",      "kulvind3r"
            VALUE "FileDescription",  "AMD Ryzen 7 250 power parameter tuning via PawnIO"
            VALUE "FileVersion",      "${VERSION}"
            VALUE "InternalName",     "R7250Adj"
            VALUE "OriginalFilename", "R7250Adj.exe"
            VALUE "ProductName",      "R7250Adj"
            VALUE "ProductVersion",   "${VERSION}"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
EOF

(cd "$SCRIPT_DIR" && "$WINDRES" "$RESOURCE_RC" -O coff -o "$RESOURCE_OBJ")

# ─── Compile ──────────────────────────────────────────────────────────────────

"$CXX" \
    -O2 \
    -static \
    -o "$OUTPUT" \
    "$SOURCE" \
    "$RESOURCE_OBJ" \
    -lshlwapi \
    -Wl,--subsystem,console

rm -f "$RESOURCE_RC" "$RESOURCE_OBJ"

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
