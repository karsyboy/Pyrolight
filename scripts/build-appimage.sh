BUILD_CONFIG="release"

fail()
{
	echo "$1" 1>&2
	exit 1
}

BUILD_ROOT=$PWD/build
SOURCE_ROOT=$PWD
BUILD_FOLDER=$BUILD_ROOT/build-$BUILD_CONFIG
DEPLOY_FOLDER=$BUILD_ROOT/deploy-$BUILD_CONFIG
INSTALLER_FOLDER=$BUILD_ROOT/installer-$BUILD_CONFIG

LINUXDEPLOY=linuxdeploy-$(uname -m).AppImage

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  VERSION=`cat $SOURCE_ROOT/app/version.txt`
fi

command -v qmake6 >/dev/null 2>&1 || fail "Unable to find 'qmake6' in your PATH!"
command -v $LINUXDEPLOY >/dev/null 2>&1 || fail "Unable to find '$LINUXDEPLOY' in your PATH!"

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $DEPLOY_FOLDER
rm -rf $INSTALLER_FOLDER
mkdir $BUILD_ROOT
mkdir $BUILD_FOLDER
mkdir $DEPLOY_FOLDER
mkdir $INSTALLER_FOLDER

# Enable LTO for official builds
export CFLAGS=-flto=auto
export CXXFLAGS=-flto=auto
export LDFLAGS=-flto=auto

echo Configuring the project
pushd $BUILD_FOLDER
# Building with Wayland support will cause linuxdeploy to include libwayland-client.so in the AppImage.
# Since we always use the host implementation of EGL, this can cause libEGL_mesa.so to fail to load due
# to missing symbols from the host's version of libwayland-client.so that aren't present in the older
# version of libwayland-client.so from our AppImage build environment. When this happens, EGL fails to
# work even in X11. To avoid this, we will disable Wayland support for the AppImage.
#
# We disable DRM support because linuxdeploy doesn't bundle the appropriate libraries for Qt EGLFS.
QMAKE_CONFIG=(CONFIG+=disable-wayland CONFIG+=disable-libdrm PREFIX=$DEPLOY_FOLDER/usr DEFINES+=APP_IMAGE)
LINUXDEPLOY_EXTRA_ARGS=()
if [ -n "${PYROWAVE_PREFIX:-}" ]; then
  PYROWAVE_PREFIX=$(readlink -f "$PYROWAVE_PREFIX")
  [ -f "$PYROWAVE_PREFIX/share/pkgconfig/pyrowave-shared.pc" ] || fail "Invalid PYROWAVE_PREFIX: pyrowave-shared.pc not found"
  [ -f "$PYROWAVE_PREFIX/lib/libpyrowave-shared.so.0" ] || fail "Invalid PYROWAVE_PREFIX: libpyrowave-shared.so.0 not found"
  [ -f "$PYROWAVE_PREFIX/share/licenses/pyrowave/LICENSE" ] || fail "Invalid PYROWAVE_PREFIX: PyroWave license not found"
  export PKG_CONFIG_PATH="$PYROWAVE_PREFIX/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  # linuxdeploy resolves the executable's DT_NEEDED entries before processing
  # explicit --library arguments, so the private PyroWave prefix must also be
  # visible to its dependency scanner.
  export LD_LIBRARY_PATH="$PYROWAVE_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  QMAKE_CONFIG+=(CONFIG+=enable-pyrowave)
  LINUXDEPLOY_EXTRA_ARGS+=(--library="$PYROWAVE_PREFIX/lib/libpyrowave-shared.so.0")
fi
qmake6 $SOURCE_ROOT/moonlight-qt.pro "${QMAKE_CONFIG[@]}" || fail "Qmake failed!"
popd

echo Compiling Moonlight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(nproc) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Deploying to staging directory
pushd $BUILD_FOLDER
make install || fail "Make install failed!"
popd

echo Staging license notices
LICENSE_DIR=$DEPLOY_FOLDER/usr/share/licenses/moonlight-qt-pyrowave
mkdir -p "$LICENSE_DIR"
cp "$SOURCE_ROOT/LICENSE" "$LICENSE_DIR/Moonlight-Qt-PyroWave-LICENSE"
if [ -n "${PYROWAVE_PREFIX:-}" ]; then
  cp "$PYROWAVE_PREFIX/share/licenses/pyrowave/LICENSE" "$LICENSE_DIR/PyroWave-LICENSE"
fi

export QML_SOURCES_PATHS=$SOURCE_ROOT/app/gui
export QMAKE=qmake6

# Stage the build-environment libva in opt/libva-fallback (outside usr/, so linuxdeploy
# does not scan it, and outside every loader search path), together with a small probe
# binary that carries the same libva ELF requirements as Moonlight. The AppRun below
# uses the probe to decide, via the dynamic loader itself, whether the host libva can
# satisfy Moonlight; when it cannot, the fallback copy is made visible. opt/ is a
# standard linuxdeploy location for application data that must not be processed.
LIBVA_FALLBACK_DIR=$DEPLOY_FOLDER/opt/libva-fallback
SYSTEM_LIBVA=$(ldconfig -p 2>/dev/null | awk '/libva\.so\.2/{print $NF; exit}')
mkdir -p $LIBVA_FALLBACK_DIR
if [ -n "$SYSTEM_LIBVA" ]; then
  cp -a "$(dirname "$SYSTEM_LIBVA")"/libva*.so* $LIBVA_FALLBACK_DIR/ || fail "Unable to stage libva fallback copy!"

  echo Compiling libva-probe
  cc -O2 -L"$(dirname "$SYSTEM_LIBVA")" -Wl,--no-as-needed -o $LIBVA_FALLBACK_DIR/libva-probe \
    $SOURCE_ROOT/app/deploy/linux/libva-probe.c -lva -lva-x11 || fail "Unable to compile libva-probe!"

  # Keep the probe honest: it must reference every VA_API_* version node that the
  # binaries shipped in the AppImage require, otherwise a host libva could pass the
  # probe and still fail to load Moonlight or the bundled FFmpeg.
  va_nodes() { LC_ALL=C readelf -V "$1" 2>/dev/null | grep -oE 'VA_API_[0-9]+\.[0-9]+\.[0-9]+' | sort -u; }
  NEEDED_NODES=$(for b in $DEPLOY_FOLDER/usr/bin/moonlight \
                          /usr/local/lib*/libav*.so* /usr/local/lib*/libsw*.so* \
                          /usr/lib/x86_64-linux-gnu/libav*.so* /usr/lib/x86_64-linux-gnu/libsw*.so*; do
                   [ -f "$b" ] && va_nodes "$b"; done | sort -u)
  PROBE_NODES=$(va_nodes $LIBVA_FALLBACK_DIR/libva-probe)
  [ -z "$(comm -13 <(echo "$PROBE_NODES") <(echo "$NEEDED_NODES"))" ] || \
    fail "libva-probe is missing version node(s): $(comm -13 <(echo "$PROBE_NODES") <(echo "$NEEDED_NODES")) - update app/deploy/linux/libva-probe.c!"
fi

APP_RUN=$BUILD_ROOT/AppRun-libva
cat > $APP_RUN <<'APPRUN_EOF'
#!/bin/bash
# AppRun: prefer the host libva; use the staged copy only if the host cannot run us.
#
# VA-API driver modules on the host are loaded by libva and export an entrypoint
# named after the libva version they were built against (__vaDriverInit_1_XX).
# A libva can only load drivers that are not newer than itself, so bundling our
# own (older) libva silently breaks hardware decoding on up-to-date distros.
# Distros keep host libva and host drivers in step, so whenever the host libva
# can satisfy Moonlight's own ELF version requirements it is the right choice.
#
# "Can satisfy" is answered here by libva-probe, a tiny program linked against
# the same versioned libva symbols as Moonlight and the bundled FFmpeg: if the
# dynamic loader can start it, the host libva works; if not (no libva installed,
# or one too old to link), we make the staged build-environment copy visible
# via LD_LIBRARY_PATH, matching the pre-existing bundled behavior.
APPDIR="${APPDIR:-$(dirname "$(readlink -f "$0")")}"
LIBVA_FALLBACK="$APPDIR/opt/libva-fallback"

if [ -d "$LIBVA_FALLBACK" ] && ! "$LIBVA_FALLBACK/libva-probe" 2>/dev/null; then
    export LD_LIBRARY_PATH="$LIBVA_FALLBACK${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export VAAPI_USE_FALLBACK_PATHS=1
fi

exec "$APPDIR/usr/bin/moonlight" "$@"
APPRUN_EOF
chmod +x $APP_RUN

echo Creating AppImage
pushd $INSTALLER_FOLDER
# Don't bundle libva: the bundled build-environment version (jammy: VA-API 1.20/1.22)
# cannot dlopen GPU drivers compiled against newer libva on the host (which export
# __vaDriverInit_1_23+), so va_openDriver() always fails and VAAPI falls back to
# software decoding. The host always provides libva on systems where VA-API is
# usable, so link against it at runtime instead (the AppRun shim above keeps a
# bundled last-resort copy for hosts without libva).
VERSION=$VERSION OUTPUT="Moonlight-$VERSION-$(uname -m).AppImage" $LINUXDEPLOY --appdir $DEPLOY_FOLDER \
  --library=/usr/local/lib/libSDL3.so.0 \
  "${LINUXDEPLOY_EXTRA_ARGS[@]}" \
  --plugin qt \
  --custom-apprun $APP_RUN \
  --exclude-library=libva.so* \
  --exclude-library=libva-drm.so* \
  --exclude-library=libva-wayland.so* \
  --exclude-library=libva-x11.so* \
  --output appimage || fail "linuxdeploy failed!"
popd

echo Build successful
