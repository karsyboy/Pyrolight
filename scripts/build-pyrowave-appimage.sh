#!/bin/bash

# Build PyroWave in the same oldest-supported userspace as Moonlight, then use
# Moonlight's normal AppImage pipeline. Run this from the Ubuntu 22.04 build
# environment documented in .github/workflows/build-appimage.yml.

set -euo pipefail

fail()
{
  echo "$1" >&2
  exit 1
}

SOURCE_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PYROWAVE_SOURCE=${PYROWAVE_SOURCE:-"$SOURCE_ROOT/../pyrowave"}
PYROWAVE_SOURCE=$(readlink -f "$PYROWAVE_SOURCE")
PYROWAVE_BUILD=${PYROWAVE_BUILD:-"$SOURCE_ROOT/build/pyrowave-appimage"}
PYROWAVE_PREFIX=${PYROWAVE_PREFIX:-"$SOURCE_ROOT/build/pyrowave-appimage-prefix"}

[ -f "$PYROWAVE_SOURCE/CMakeLists.txt" ] || fail "Set PYROWAVE_SOURCE to the patched PyroWave source tree"

if [ ! -d "$PYROWAVE_SOURCE/Granite" ]; then
  (cd "$PYROWAVE_SOURCE" && ./checkout_granite.sh)
fi

cmake -S "$PYROWAVE_SOURCE" -B "$PYROWAVE_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PYROWAVE_PREFIX" \
  -DPYROWAVE_DEVEL=OFF \
  -DPYROWAVE_UTILS=OFF
cmake --build "$PYROWAVE_BUILD" -j"$(nproc)"
cmake --install "$PYROWAVE_BUILD"

export PYROWAVE_PREFIX
cd "$SOURCE_ROOT"
exec "$SOURCE_ROOT/scripts/build-appimage.sh"
