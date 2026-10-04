# Contributing to Moonlight Qt PyroWave

Bug fixes, documentation improvements, and focused features are welcome. Report
bugs with the issue template; include the client release, OS, GPU and driver,
host version, stream settings, and client and host logs. Remove credentials,
host addresses, and other private data before sharing logs.

Keep changes focused and explain the resulting behavior and validation in your
pull request. Preserve Moonlight's GPL-3.0 copyright and license notices.
General Moonlight behavior should stay as close to upstream as practical; see
[AGENTS.md](AGENTS.md) for the fork and upstream policy.

## Building from source

Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/karsyboy/moonlight-qt-pyrowave.git
cd moonlight-qt-pyrowave
```

### PyroWave dependency

PyroWave support requires the patched standalone C API from the pinned
[`karsyboy/pyrowave`](https://github.com/karsyboy/pyrowave) revision:

| Item | Value |
| --- | --- |
| Revision | `e344479d6c0439e346c788a918ad5645713f7573` |
| C API | `pyrowave-shared` 0.7.0 (checked by `app/app.pro`) |
| Granite | `1b2d1801d2910fb09ebcded2f0bb3a3a781103b5`, from `checkout_granite.sh` |
| Patches | `scripts/pyrowave-patches/*.patch`, applied after `checkout_granite.sh` |

Do not substitute upstream PyroWave 0.6 or another ABI. Build and install it
without the development applications:

```sh
git clone https://github.com/karsyboy/pyrowave.git deps/PyroWave
git -C deps/PyroWave checkout e344479d6c0439e346c788a918ad5645713f7573
(cd deps/PyroWave && ./checkout_granite.sh)
for patch in scripts/pyrowave-patches/*.patch; do git -C deps/PyroWave apply "$PWD/$patch"; done
cmake -S deps/PyroWave -B build/pyrowave \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/pyrowave-install" \
  -DPYROWAVE_DEVEL=OFF -DPYROWAVE_UTILS=OFF
cmake --build build/pyrowave --parallel
cmake --install build/pyrowave
```

The patches add full-resolution 4:4:4 payload allocation and 200-nit
normalization when converting PQ/scRGB into SDR. The release workflows apply
the same patches. The Pyroshine host pins a newer revision of the same fork; the
two pins need not match, but the client must decode the host's block-format
family (`186f0393`). See [PyroWave integration](docs/PYROWAVE.md#dependency-and-build-contract)
before changing the pin.

### Linux

Install Qt 6, CMake, Vulkan development tools, FFmpeg, libplacebo, SDL2,
SDL2_ttf, OpenSSL, Opus, and the VA-API/VDPAU/X11 development packages for
your distribution. With PyroWave installed as above:

```sh
export PKG_CONFIG_PATH="$PWD/build/pyrowave-install/share/pkgconfig:$PKG_CONFIG_PATH"
qmake6 moonlight-qt.pro CONFIG+=enable-pyrowave
make -j"$(nproc)" release
```

Omit `CONFIG+=enable-pyrowave` to build without PyroWave.

The release AppImage is built in an Ubuntu 22.04 userspace; the complete
dependency list is in [build-appimage.yml](.github/workflows/build-appimage.yml).
In that environment, build PyroWave (with patches) and the AppImage with:

```sh
PYROWAVE_SOURCE="$PWD/deps/PyroWave" scripts/build-pyrowave-appimage.sh
```

### Windows x64

Install Visual Studio 2022, Qt 6.10.2 with the MSVC 2022 x64 kit, CMake, Git
Bash, and 7-Zip. From a Qt-enabled PowerShell, download dependencies and build
PyroWave as above (run the clone, Granite checkout and patch steps in Git Bash),
adding `-A x64` to the CMake configure step and `--config Release` to the build
and install steps. Then:

```powershell
./setup-deps.ps1 -Architecture x64
$env:PYROWAVE_PREFIX = "$PWD/build/pyrowave-install"
$env:PYROWAVE_SOURCE = "$PWD/deps/PyroWave"
cmd /c scripts\build-arch.bat Release
cmd /c scripts\generate-bundle.bat Release
```

`build-arch.bat` enables the integration when `PYROWAVE_PREFIX` is set and
stages the runtime DLL and license. See [release.yml](.github/workflows/release.yml)
for the exact CI sequence.

## Validation

CI builds release artifacts but does not run the test suites below; run the
relevant ones locally.

Client unit tests (SDL2 and libplacebo through pkg-config):

```sh
cmake -S tests -B build/tests
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

They cover DualSense Edge identity and capability compatibility, PyroWave color
mapping, frame validation, record parsing and recovery, bandwidth math, and
golden bitstreams from both the client and host forks. When Qt5, OpenSSL and a
built `moonlight-common-c` library are available, the project also builds
`pyrowave-https`; run its TLS calibration harness with:

```sh
python3 tests/pyrowave_https.py build/tests/pyrowave-https
```

Protocol tests live in the `moonlight-common-c` submodule:

```sh
cmake -S moonlight-common-c/moonlight-common-c -B build/common-c-tests \
  -DPYROWAVE_PROTOCOL_TESTS=ON -DCONTROLLER_PROTOCOL_TESTS=ON
cmake --build build/common-c-tests
ctest --test-dir build/common-c-tests --output-on-failure
```

Streaming-profile persistence has a Qt test:

```sh
qmake6 app/tests/streamingpreferences_test.pro -o build/prefs-test/Makefile
make -C build/prefs-test
./build/prefs-test/streamingpreferences-test
```

The PyroWave renderer smoke test needs a Vulkan-capable display; see
[renderer smoke test](docs/PYROWAVE.md#renderer-smoke-test).

Automated tests do not establish live HDR output, sustained bitrate, NIC
behavior, reconnects or controller hardware. For decoder, renderer, network or
protocol changes, test against a real Pyroshine host and report the hardware,
platforms and checks you could not run.

## Publishing a release

Push an annotated tag in the form `vMAJOR.MINOR.PATCH` (optionally `.BUILD`):

```sh
# Example only: replace with the version being released.
git tag -a vX.Y.Z -m "Moonlight Qt PyroWave vX.Y.Z"
git push origin vX.Y.Z
```

The [release workflow](.github/workflows/release.yml) derives the application
version from the tag, builds the Windows installer and portable ZIP and the
Linux AppImage against the pinned PyroWave revision, verifies the PyroWave
runtime is bundled, and publishes a GitHub Release with `SHA256SUMS` and
generated notes. Test installation of each asset afterwards.
