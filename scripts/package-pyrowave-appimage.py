#!/usr/bin/env python3
"""Package an existing native PyroWave build, with a private glibc runtime.

GPU drivers remain on the host. This is a Linux x86_64 test distribution,
not the upstream oldest-distribution AppImage build. See docs/pyrowave.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--smoke-binary", type=Path, required=True)
    parser.add_argument("--pyrowave-prefix", type=Path, required=True)
    parser.add_argument("--appdir", type=Path, required=True)
    parser.add_argument("--appimagetool", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if "libpyrowave-shared" not in run("readelf", "-d", str(args.binary)):
        raise SystemExit("The client binary must be built with CONFIG+=enable-pyrowave")
    source = Path(__file__).resolve().parents[1]
    appdir = args.appdir.resolve()
    if appdir.exists():
        raise SystemExit("AppDir already exists; choose a fresh output directory")
    lib = appdir / "usr/lib"
    bin_dir = appdir / "usr/bin"
    lib.mkdir(parents=True)
    bin_dir.mkdir(parents=True)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = str(args.pyrowave_prefix.resolve() / "lib")
    copied = {}
    scanned = set()
    owners = set()

    def collect(path):
        path = Path(path)
        resolved = path.resolve()
        if resolved in scanned:
            return
        scanned.add(resolved)
        output = run("ldd", str(resolved), env=env, stderr=subprocess.STDOUT)
        if "not found" in output:
            raise RuntimeError(f"Unresolved dependencies for {path}:\n{output}")
        for line in output.splitlines():
            match = re.search(r"(?:=>\s*)?(/\S+)\s+\(", line)
            if match:
                dependency = Path(match[1])
                destination = lib / dependency.name
                if destination.name not in copied:
                    shutil.copy2(dependency.resolve(), destination)
                    copied[destination.name] = str(dependency.resolve())
                    try:
                        owner = run("pacman", "-Qqo", str(dependency.resolve()), stderr=subprocess.DEVNULL).strip()
                        owners.add(owner)
                    except subprocess.CalledProcessError:
                        pass
                collect(dependency)

    for executable, name in [(args.binary, "moonlight"), (args.smoke_binary, "pyrowave-render-smoke")]:
        collect(executable)
        shutil.copy2(executable, bin_dir / name)
        subprocess.check_call(["strip", "--strip-unneeded", str(bin_dir / name)])

    plugins = Path(run("qmake6", "-query", "QT_INSTALL_PLUGINS").strip())
    plugin_out = appdir / "usr/plugins"
    for category in ("platforms", "imageformats", "iconengines", "tls", "networkinformation",
                     "wayland-decoration-client", "wayland-graphics-integration-client", "wayland-shell-integration"):
        if (plugins / category).is_dir():
            if category in ("imageformats", "iconengines"):
                (plugin_out / category).mkdir(parents=True)
                for plugin in (plugins / category).glob("libq*.so"):
                    shutil.copy2(plugin, plugin_out / category / plugin.name)
            else:
                shutil.copytree(plugins / category, plugin_out / category)
    # Use the portal theme, without loading host KDE/GTK Qt plugins into our Qt.
    (plugin_out / "platformthemes").mkdir(parents=True)
    portal = plugins / "platformthemes/libqxdgdesktopportal.so"
    if portal.exists():
        shutil.copy2(portal, plugin_out / "platformthemes" / portal.name)
    for path in plugin_out.rglob("*.so"):
        collect(path)

    qml = Path(run("qmake6", "-query", "QT_INSTALL_QML").strip())
    for module in ("QtQml", "QtQuick", "Qt5Compat"):
        if (qml / module).is_dir():
            shutil.copytree(qml / module, appdir / "usr/qml" / module)
    for path in (appdir / "usr/qml").rglob("*.so"):
        collect(path)

    # SDL2-compat, audio/input backends and name-service modules are dlopened.
    for pattern in ("libSDL3.so.0", "libasound.so.2", "libpulse.so.0", "libpulse-simple.so.0",
                    "libpipewire-0.3.so.0", "libudev.so.1", "libdecor-0.so.0",
                    "libwayland-cursor.so.0", "libwayland-egl.so.1", "libnss_*.so.2",
                    "libresolv.so.2"):
        for path in Path("/usr/lib").glob(pattern):
            shutil.copy2(path.resolve(), lib / path.name)
            copied[path.name] = str(path.resolve())
            collect(path)
    for directory in ("alsa-lib", "libdecor/plugins-1"):
        original = Path("/usr/lib") / directory
        if original.is_dir():
            shutil.copytree(original, lib / directory)
            for path in (lib / directory).rglob("*.so"):
                collect(path)

    translations = Path(run("qmake6", "-query", "QT_INSTALL_TRANSLATIONS").strip())
    shutil.copytree(translations, appdir / "usr/translations")
    (bin_dir / "qt.conf").write_text("[Paths]\nPrefix=..\nPlugins=plugins\nQmlImports=qml\nTranslations=translations\n")
    desktop = (source / "app/deploy/linux/com.moonlight_stream.Moonlight.desktop").read_text()
    desktop = desktop.replace("Name=Moonlight", "Name=Moonlight PyroWave").replace("Exec=moonlight", "Exec=AppRun")
    (appdir / "moonlight.desktop").write_text(desktop)
    icon = source / "app/deploy/linux/moonlight.svg"
    if not icon.exists():
        icon = next((source / "app").rglob("moonlight.svg"))
    shutil.copy2(icon, appdir / "moonlight.svg")
    (appdir / ".DirIcon").symlink_to("moonlight.svg")
    (appdir / "AppRun").write_text('''#!/bin/sh
set -eu
APPDIR=${APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}
export QT_PLUGIN_PATH="$APPDIR/usr/plugins"
export QML2_IMPORT_PATH="$APPDIR/usr/qml"
export QML_IMPORT_PATH="$APPDIR/usr/qml"
export QT_QUICK_CONTROLS_STYLE=${QT_QUICK_CONTROLS_STYLE:-Material}
export QT_QPA_PLATFORMTHEME=xdgdesktopportal
# Private runtime is passed to the loader, not inherited by host child programs.
unset LD_PRELOAD LD_AUDIT
# This fallback packager mixes a private glibc/Vulkan userspace with host GPU
# drivers. Do not allow GameScope's host implicit Vulkan layer into that mixed
# process: it can abort in vkroots before Moonlight creates its first window.
# The normal Ubuntu 22.04 AppImage build does not need this compatibility path.
if [ -n "${GAMESCOPE_WAYLAND_DISPLAY:-}" ] || [ "${SteamDeck:-0}" = 1 ]; then
    export ENABLE_GAMESCOPE_WSI=0
    export VK_LOADER_LAYERS_DISABLE="*gamescope*${VK_LOADER_LAYERS_DISABLE:+,$VK_LOADER_LAYERS_DISABLE}"
    export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-xcb}
    export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-x11}
fi
program=moonlight
if [ "${1:-}" = --pyrowave-render-test ]; then
    shift
    program=pyrowave-render-smoke
    export QT_QPA_PLATFORM=offscreen
fi
exec "$APPDIR/usr/lib/ld-linux-x86-64.so.2" \\
    --library-path "$APPDIR/usr/lib:$APPDIR/usr/lib/pulseaudio" \\
    "$APPDIR/usr/bin/$program" "$@"
''')
    (appdir / "AppRun").chmod(0o755)

    license_dir = appdir / "usr/share/licenses"
    license_dir.mkdir(parents=True)
    shutil.copy2(source / "LICENSE", license_dir / "Moonlight-LICENSE")
    for owner in owners:
        original = Path("/usr/share/licenses") / owner
        if original.is_dir():
            shutil.copytree(original, license_dir / owner, dirs_exist_ok=True)
    shutil.copytree("/usr/share/licenses/spdx", license_dir / "spdx")
    pyro_source = args.pyrowave_prefix.resolve().parents[1] / "pyrowave"
    if not pyro_source.exists():
        pyro_source = source.parent / "pyrowave"
    shutil.copy2(pyro_source / "LICENSE", license_dir / "PyroWave-LICENSE")
    if (pyro_source / "Granite/LICENSE").exists():
        shutil.copy2(pyro_source / "Granite/LICENSE", license_dir / "Granite-LICENSE")
    manifest = {"libraries": copied, "packages": sorted(owners),
                "runtime_sha256": hashlib.sha256(args.runtime.read_bytes()).hexdigest(),
                "packager_sha256": hashlib.sha256(args.appimagetool.read_bytes()).hexdigest(),
                "gpu_drivers": "host supplied", "glibc": run("ldd", "--version").splitlines()[0]}
    (appdir / "usr/share/runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.check_call([str(args.appimagetool.resolve()), "--runtime-file", str(args.runtime.resolve()),
                           str(appdir), str(args.output.resolve())], env={**os.environ, "ARCH": "x86_64",
                               "PATH": str(args.appimagetool.resolve().parent) + os.pathsep + os.environ["PATH"]})
    print(args.output.resolve())


if __name__ == "__main__":
    main()
