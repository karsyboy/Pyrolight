# Moonlight Qt PyroWave

**Moonlight Qt PyroWave is a Moonlight Qt fork with PyroWave support.** It keeps
Moonlight's low-latency game-streaming client and adds an explicitly selectable
PyroWave video path for compatible
[Pyroshine](https://github.com/karsyboy/pyroshine) hosts.

This is an independent community fork. It is not an official Moonlight or
PyroWave project.

## Why this fork exists

[PyroWave](https://github.com/karsyboy/pyrowave) is an intra-only video codec
designed for very low GPU encode/decode latency on high-bandwidth networks.
Upstream Moonlight Qt does not include this codec, so this repository maintains
the client integration and release packaging needed to use it.

The ordinary H.264, HEVC, and AV1 Moonlight paths remain available. PyroWave is
never selected automatically.

## PyroWave support

The client exposes **PyroWave** in the video codec setting when its Vulkan
runtime probe succeeds. The implementation:

- decodes on the GPU through the patched PyroWave 0.7 C API;
- shares the Vulkan device and GPU-local image planes with Moonlight's
  libplacebo renderer, without decoded-pixel CPU staging;
- supports YUV 4:2:0 and 4:4:4; and
- supports SDR and PQ/BT.2020 HDR using Moonlight's existing HDR controls and
  host-provided mastering metadata.

See [docs/pyrowave.md](docs/pyrowave.md) for the integration details, pinned
dependency revision, limits, and developer test procedure.

## Supported platforms

This fork publishes and maintains:

- Windows 10 or later, x64: installer and portable ZIP
- Linux, x86_64: AppImage

The source tree retains shared upstream platform code, but this project does
not publish macOS, Steam Link, ARM, Snap, Flatpak, or distribution packages.

## Downloads

Download tagged builds from this repository's
[GitHub Releases](https://github.com/karsyboy/moonlight-qt-pyrowave/releases).
Release assets include the project name, tag, operating system, and
architecture. `SHA256SUMS` is attached to each release.

## Installation

### Windows

Download the `windows-x64-installer.exe` asset and run it. To avoid installing,
download the `windows-x64-portable.zip` asset, extract it, and run
`Moonlight.exe`.

### Linux

Download the `linux-x86_64.AppImage` asset, make it executable, and run it:

```sh
chmod +x moonlight-qt-pyrowave-v*-linux-x86_64.AppImage
./moonlight-qt-pyrowave-v*-linux-x86_64.AppImage
```

## Compatibility and requirements

For normal Moonlight codecs, use Pyroshine, a compatible Sunshine host, or a
legacy NVIDIA GameStream host as supported by upstream Moonlight Qt.

PyroWave additionally requires:

- a release from this repository with the PyroWave runtime bundled;
- a Vulkan 1.3-capable GPU and driver that pass the startup decoder probe;
- a compatible [Pyroshine](https://github.com/karsyboy/pyroshine) release; and
- a network that can sustain the selected bitrate.

On the host, enable the PyroWave encoder. In this client, select **PyroWave**
under video codec and enable HDR or YUV 4:4:4 only when the host and display
support them. The client accepts up to 2,000,000 Kbps and 240 FPS, subject to an
encoded-frame budget of 1 KiB to 3 MiB. Unsupported server capability, device,
build, or frame-budget combinations fail with an error rather than silently
falling back.

V-Sync remains a user-selectable Moonlight setting. If a high-refresh PyroWave
stream is unexpectedly presentation-limited, test with V-Sync disabled so the
client can use the lowest-latency present mode supported by the Vulkan driver.
This is a troubleshooting step, not a universal PyroWave requirement.

## Building from source

Initialize this repository first:

```sh
git clone --recurse-submodules https://github.com/karsyboy/moonlight-qt-pyrowave.git
cd moonlight-qt-pyrowave
```

Release builds use PyroWave commit
`e344479d6c0439e346c788a918ad5645713f7573`, whose standalone C API reports
version 0.7.0. Do not substitute the incompatible upstream 0.6 API.

### Windows x64

Install Visual Studio 2022, Qt 6.10.2 with the MSVC 2022 x64 kit, CMake, Git
Bash, and 7-Zip. From a Qt-enabled PowerShell/Git Bash environment:

```powershell
./setup-deps.ps1 -Architecture x64
git clone https://github.com/karsyboy/pyrowave.git deps/PyroWave
git -C deps/PyroWave checkout e344479d6c0439e346c788a918ad5645713f7573
bash -lc "cd deps/PyroWave && ./checkout_granite.sh"
cmake -S deps/PyroWave -B build/pyrowave -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_INSTALL_PREFIX="$PWD/build/pyrowave-install" `
  -DPYROWAVE_DEVEL=OFF -DPYROWAVE_UTILS=OFF
cmake --build build/pyrowave --config Release --parallel
cmake --install build/pyrowave --config Release
$env:PYROWAVE_PREFIX = "$PWD/build/pyrowave-install"
$env:PYROWAVE_SOURCE = "$PWD/deps/PyroWave"
cmd /c scripts\build-arch.bat Release
cmd /c scripts\generate-bundle.bat Release
```

### Linux

Install Qt 6, CMake, Vulkan development tools, FFmpeg, libplacebo, SDL2,
SDL2_ttf, OpenSSL, Opus, and the VA-API/VDPAU/X11 development packages used by
your distribution. Build and install the pinned PyroWave C API, add its
`share/pkgconfig` directory to `PKG_CONFIG_PATH`, then run:

```sh
qmake6 moonlight-qt.pro CONFIG+=enable-pyrowave
make -j"$(nproc)" release
```

For a release-equivalent AppImage environment, follow
[docs/pyrowave.md](docs/pyrowave.md) and use
`scripts/build-pyrowave-appimage.sh`.

## Relationship to upstream projects

This project is derived from
[Moonlight Qt / Moonlight PC](https://github.com/moonlight-stream/moonlight-qt)
and periodically carries upstream client code. General Moonlight usage and
troubleshooting documentation remains available from the
[Moonlight project](https://moonlight-stream.org/).

The codec dependency is the pinned
[`karsyboy/pyrowave`](https://github.com/karsyboy/pyrowave) fork, which is
derived from [Themaister's PyroWave](https://github.com/Themaister/pyrowave).
This repository does not claim to be either upstream project.

## Licensing and attribution

Moonlight Qt PyroWave remains licensed under the
[GNU General Public License v3](LICENSE), preserving Moonlight's copyright and
license notices. PyroWave is distributed under its own MIT license, which is
included with packaged runtime components. Bundled dependencies retain their
respective licenses and notices.
