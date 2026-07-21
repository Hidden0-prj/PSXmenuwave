#!/bin/bash

# 1. Setup paths
ROOT_DIR="$(pwd)"
TOOLCHAIN_DIR="$ROOT_DIR/gcc-mipsel-none-elf"

echo "Downloading official ps1-bare-metal SDK environment..."
git clone --depth 1 https://github.com/spicyjpeg/ps1-bare-metal.git sdk || exit 1
cd sdk || exit 1

echo "Locating a suitable example folder to base your demo on..."
# The SDK doesn't have a generic "src/main" folder - each tutorial step lives
# in its own numbered folder (e.g. src/08_spinningCube). We need one that
# already wires up the GTE + ordering-table GPU helpers our main.c relies on
# (clearOrderingTable / GPU_ORDERING_TABLE_SIZE / GPUDMAChain.orderingTable /
# allocateGP0Packet(chain, zIndex, count)), so we replace ITS main.c in place
# instead of trying to register a brand new CMake target.
TARGET_DIR=""
for candidate in src/08_spinningCube src/07_orderingTable; do
    if [ -f "$candidate/main.c" ]; then
        TARGET_DIR="$candidate"
        break
    fi
done
if [ -z "$TARGET_DIR" ]; then
    MATCH="$(grep -rl "clearOrderingTable" --include=main.c src 2>/dev/null | head -n1)"
    [ -n "$MATCH" ] && TARGET_DIR="$(dirname "$MATCH")"
fi

if [ -z "$TARGET_DIR" ] || [ ! -d "$TARGET_DIR" ]; then
    echo "ERROR: could not find a suitable example folder inside the cloned SDK." >&2
    echo "The upstream ps1-bare-metal repo structure may have changed. Folders found under src/:" >&2
    find src -maxdepth 1 -type d >&2
    exit 1
fi
echo "Using $TARGET_DIR as the base for your model demo."

echo "Injecting your custom 3D model and game code..."
cp "$ROOT_DIR/src/main.c"         "$TARGET_DIR/main.c"         || exit 1
cp "$ROOT_DIR/src/model_data.h"   "$TARGET_DIR/model_data.h"   || exit 1
cp "$ROOT_DIR/src/texture_data.h" "$TARGET_DIR/texture_data.h" || exit 1
cp "$ROOT_DIR/src/font_data.h"    "$TARGET_DIR/font_data.h"    || exit 1

echo "Checking for the MIPS toolchain..."
export PATH=$PATH:$TOOLCHAIN_DIR/bin
if ! command -v mipsel-none-elf-gcc >/dev/null 2>&1; then
    echo "ERROR: mipsel-none-elf-gcc was not found on PATH ($TOOLCHAIN_DIR/bin)." >&2
    echo "This usually means the 'Build GCC toolchain' step didn't run (e.g. a stale/corrupt actions/cache entry was restored instead)." >&2
    echo "Try bumping the cache key in .github/workflows/main.yml and re-running." >&2
    ls -la "$TOOLCHAIN_DIR/bin" 2>&1 >&2 || echo "($TOOLCHAIN_DIR/bin does not exist)" >&2
    exit 1
fi
mipsel-none-elf-gcc --version

echo "Setting up the SDK's Python virtual environment..."
# The SDK's own CMake scripts (cmake/tools.cmake) require a venv at ./env
# with tools/requirements.txt installed - this is used by its build-time
# asset/psexe packaging tools, separate from our obj/png converter scripts.
if [ -f tools/requirements.txt ]; then
    python3 -m venv env || exit 1
    ./env/bin/pip install --quiet --upgrade pip || exit 1
    ./env/bin/pip install --quiet -r tools/requirements.txt || exit 1
else
    echo "WARNING: sdk/tools/requirements.txt not found, creating an empty venv anyway." >&2
    python3 -m venv env || exit 1
fi

echo "Building PlayStation Executable..."
cmake --preset release -DTOOLCHAIN_PATH="$TOOLCHAIN_DIR" . || exit 1
cmake --build build || exit 1

echo "Packaging the final game files..."
RELEASE_NAME="PS1_Model_Release"
mkdir -p "$ROOT_DIR/$RELEASE_NAME"

# The SDK's CMake project builds ALL example folders, not just ours, so
# "build/*.psexe" would match several files. Find the one that matches our
# target folder's name specifically.
TARGET_NAME="$(basename "$TARGET_DIR")"
PSEXE_FILE="$(find build -iname "*${TARGET_NAME}*.psexe" 2>/dev/null | head -n1)"
if [ -z "$PSEXE_FILE" ]; then
    PSEXE_FILE="$(find build -iname "*.psexe" 2>/dev/null | head -n1)"
fi
if [ -z "$PSEXE_FILE" ]; then
    echo "ERROR: no .psexe file was produced by the build." >&2
    find build -maxdepth 2 >&2
    exit 1
fi
echo "Found built executable: $PSEXE_FILE"
cp "$PSEXE_FILE" "$ROOT_DIR/$RELEASE_NAME/game.psexe"

BIN_FILE="$(find build -iname "*${TARGET_NAME}*.bin" 2>/dev/null | head -n1)"
CUE_FILE="$(find build -iname "*${TARGET_NAME}*.cue" 2>/dev/null | head -n1)"
[ -n "$BIN_FILE" ] && cp "$BIN_FILE" "$ROOT_DIR/$RELEASE_NAME/game.bin"
[ -n "$CUE_FILE" ] && cp "$CUE_FILE" "$ROOT_DIR/$RELEASE_NAME/game.cue"

cd "$ROOT_DIR"
zip -9 -r "$RELEASE_NAME.zip" "$RELEASE_NAME" || exit 3

echo "SUCCESS: Your PS1 build is complete!"
