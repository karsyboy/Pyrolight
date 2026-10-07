#!/bin/sh
# /usr/bin/pyrolight for the distribution packages, which install the AppImage
# payload in /opt/pyrolight. AppRun applies the same host-library checks
# (libwayland-client, libva) as the AppImage.
APPDIR=/opt/pyrolight
export APPDIR
exec "$APPDIR/AppRun" "$@"
