#!/bin/bash
# Check that a Pyrolight AppDir or package staging root uses the Pyrolight icon
# everywhere: every desktop entry names the "pyrolight" icon, every installed
# icon is app/res/moonlight.svg (the generated Pyrolight artwork), and no icon
# is installed under the name "moonlight", which icon themes replace with
# Moonlight's logo.

set -uo pipefail

ROOT=${1:?usage: $0 <AppDir-or-staging-root>}
SOURCE_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ICON=$SOURCE_ROOT/app/res/moonlight.svg
failures=0

check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1" >&2; failures=$((failures + 1)); fi; }

grep -q '<title>Pyrolight</title>' "$ICON" || { echo "FAIL: $ICON is not the generated Pyrolight icon" >&2; exit 1; }

desktops=$(find "$ROOT" -name '*.desktop' -not -path '*/qml/*')
check "Desktop entries exist" '[ -n "$desktops" ]'
for desktop in $desktops; do
    check "${desktop#$ROOT/} uses Icon=pyrolight" 'grep -qx "Icon=pyrolight" "$desktop"'
done

icons=$(find "$ROOT" \( -path '*/icons/*' -o -name '.DirIcon' -o -name '*.svg' -o -name '*.png' \) \
             \( -type f -o -type l \) -not -path '*/qml/*' -not -path '*/plugins/*')
check "Icons exist" '[ -n "$icons" ]'
for icon in $icons; do
    check "${icon#$ROOT/} is the Pyrolight icon" 'cmp -s "$icon" "$ICON"'
done
stale=$(find "$ROOT" -path '*/icons/*' -iname 'moonlight*')
check "No icon is installed under the moonlight name${stale:+ (found: $stale)}" '[ -z "$stale" ]'

if [ $failures -ne 0 ]; then
    echo "$failures icon check(s) failed" >&2
    exit 1
fi
echo "Icon checks passed"
