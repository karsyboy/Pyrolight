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
[ "$(git -C "$PYROWAVE_SOURCE/Granite" rev-parse HEAD 2>/dev/null)" = "1b2d1801d2910fb09ebcded2f0bb3a3a781103b5" ] || \
  fail "PyroWave's pinned Granite checkout is missing or incorrect"

# Apply the audited, wire-compatible fixes to the pinned dependency.
patch_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/pyrowave-patches" && pwd)"
for patch in "$patch_dir"/*.patch; do
  if git -C "${PYROWAVE_SOURCE}" apply --check "$patch"; then
    git -C "${PYROWAVE_SOURCE}" apply "$patch"
  else
    git -C "${PYROWAVE_SOURCE}" apply --reverse --check "$patch"
  fi
done

cmake -S "$PYROWAVE_SOURCE" -B "$PYROWAVE_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PYROWAVE_PREFIX" \
  -DPYROWAVE_DEVEL=OFF \
  -DPYROWAVE_UTILS=OFF
cmake --build "$PYROWAVE_BUILD" -j"$(nproc)"
cmake --install "$PYROWAVE_BUILD"
mkdir -p "$PYROWAVE_PREFIX/share/licenses/pyrowave"
cp "$PYROWAVE_SOURCE/LICENSE" "$PYROWAVE_PREFIX/share/licenses/pyrowave/LICENSE"

export PYROWAVE_PREFIX
cd "$SOURCE_ROOT"
exec "$SOURCE_ROOT/scripts/build-appimage.sh"
