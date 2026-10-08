# Linux distribution packages

Every release ships the AppImage and three packages built from the same
payload: `pyrolight_<version>-1_amd64.deb`, `pyrolight-<version>-1.x86_64.rpm`
and `pyrolight-<version>-1-x86_64.pkg.tar.zst`. This document describes their
layout, dependencies and icon handling.

## Layout

Pyrolight needs pinned SDL3, sdl2-compat, FFmpeg, libplacebo and PyroWave builds
that distributions do not ship, so the packages install the AppImage payload
instead of linking distribution libraries.

| Path | Content |
| --- | --- |
| `/opt/pyrolight/` | The extracted, verified AppImage (bundled Qt, SDL, FFmpeg, libplacebo, PyroWave, the libwayland-client and libva fallbacks and their probes) |
| `/usr/bin/pyrolight` | Launcher that runs `/opt/pyrolight/AppRun`, so the host-library probes in [LINUX_DISPLAY.md](LINUX_DISPLAY.md#appimage-packaging-boundary) apply unchanged |
| `/usr/share/applications/com.moonlight_stream.Moonlight.desktop` | Desktop entry (`Name=Pyrolight`, `Icon=pyrolight`) |
| `/usr/share/icons/hicolor/scalable/apps/pyrolight.svg` | Pyrolight icon |
| `/usr/share/metainfo/com.moonlight_stream.Moonlight.appdata.xml` | AppStream metadata |
| `/usr/share/licenses/pyrolight/`, `/usr/share/doc/pyrolight/` | Pyrolight and PyroWave licenses (per-distribution location) |

`scripts/build-linux-packages.sh` stages these from an AppDir, makes the payload
world-readable (AppImage directories are mode 0700), checks the icons and runs
nfpm with [app/deploy/linux/nfpm.yaml](../app/deploy/linux/nfpm.yaml) for each
format. The release workflow builds them from the AppImage it has just
verified.

## Dependencies

The packages depend on what the payload links against but does not bundle,
plus the libraries SDL loads at runtime: glibc 2.35 and libstdc++ from GCC 12
or later (the Ubuntu 22.04 build environment), GL/EGL (GLVND), X11 and XCB,
libwayland, libdrm, fontconfig, FreeType, HarfBuzz, zlib, libudev (controller
hotplug) and ALSA. Package names differ per distribution and are listed in
`nfpm.yaml`.

| Distribution family | Optional (recommended) |
| --- | --- |
| Debian / Ubuntu | `libva2`, `libva-x11-2`, `libva-wayland2`, `libpulse0`, `libdecor-0-0`, a Vulkan driver |
| Fedora / RHEL | `libva`, `pulseaudio-libs`, `libdecor`, `mesa-vulkan-drivers` |
| Arch | `libva` is required (nfpm cannot express Arch optional dependencies) |

The glibc requirement makes RHEL 9 (glibc 2.34) refuse the package; RHEL 10
and later work.

The packages keep Moonlight's desktop ID, AppStream ID and settings location,
so they conflict with `moonlight-qt` (packaged in Arch's `extra`). Installing
Pyrolight replaces it; existing Moonlight settings and paired hosts carry over.

## Icon name

The desktop entry and icon use the name `pyrolight`. Icon themes such as Papirus
provide their own `moonlight` and `com.moonlight_stream.Moonlight` icons, and a
theme icon takes precedence over the application's `hicolor` icon. On Wayland,
KWin and other compositors show the icon named by the desktop entry that matches
the app ID (`com.moonlight_stream.Moonlight`), so with `Icon=moonlight` themed
desktops would show Moonlight's logo in menus, on the task bar and in title bars.
No theme ships `pyrolight`, so it resolves to the Pyrolight icon in `hicolor`.

All Pyrolight icon files are generated from `assets/logo-no-text.png` by
`scripts/generate-branding.py` at their upstream paths (`app/res/moonlight.svg`
and others); only the installed name differs.

## Pacman repository

`pyrolight-bin` in the
[`[pyrowave]` pacman repository](https://github.com/karsyboy/pyrowave-packages)
repackages the release's `pyrolight-<version>-1-x86_64.pkg.tar.zst` with the same
layout and dependencies, so `pacman -Syu` installs and upgrades the same payload.
It renames the license directory to `/usr/share/licenses/pyrolight-bin` and
conflicts with `moonlight-qt`, which owns the same desktop and AppStream IDs. Keep its PKGBUILD's `depends` in sync
with the `archlinux` override in `nfpm.yaml`.

## Validation

Build and check the packages from an extracted AppImage:

```sh
./Pyrolight-*.AppImage --appimage-extract
scripts/check-linux-icons.sh squashfs-root
VERSION=6.2.1 scripts/build-linux-packages.sh squashfs-root build/packages
```

Install each package on a clean system of its family, then check that every
bundled library resolves, that `pyrolight` starts (the log shows
`Display backend:`), that the menu entry shows the Pyrolight icon with an icon
theme such as Papirus, and that removal deletes `/opt/pyrolight`:

```sh
find /opt/pyrolight/usr -type f -name '*.so*' -exec sh -c \
  'LD_LIBRARY_PATH=/opt/pyrolight/usr/lib ldd "$1" | grep "not found"' _ {} \;
pyrolight
```
