# Native PyroWave on Linux

PyroWave is explicitly selected in Settings or the CLI. Automatic selection retains the existing codecs. New server capability bits alone do not select PyroWave: the private SDP version marker must also match. Unsupported host/device/build combinations produce errors.

## Build the patched dependency

Use the PyroWave checkout delivered with this patch. Its API version is **0.7.0 (local patch, not an upstream release)**, based on `89f7e47d4abbf650c91fae766728af866c5e32a0`. Granite is pinned to `1b2d1801d2910fb09ebcded2f0bb3a3a781103b5` by PyroWave's `checkout_granite.sh`. Do not substitute upstream 0.6 or an arbitrary newer API.

```sh
export PW_ROOT=/home/karsy/Projects/Pyrowave
export PW_PREFIX="$PW_ROOT/build/pyrowave-install"
cmake -S "$PW_ROOT/pyrowave" -B "$PW_ROOT/build/pyrowave" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PW_PREFIX" \
  -DPYROWAVE_DEVEL=OFF -DPYROWAVE_UTILS=OFF
cmake --build "$PW_ROOT/build/pyrowave" -j4
cmake --install "$PW_ROOT/build/pyrowave"
export PKG_CONFIG_PATH="$PW_PREFIX/share/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$PW_PREFIX/lib:$LD_LIBRARY_PATH"
```

Both applications use the installed standalone shared C API through pkg-config. Neither downloads PyroWave during configuration. Their patched moonlight-common-c checkouts must accompany the application patch.

## Build and use Moonlight Qt

```sh
mkdir -p "$PW_ROOT/build/moonlight-pyrowave"
cd "$PW_ROOT/build/moonlight-pyrowave"
qmake6 "$PW_ROOT/moonlight-qt/moonlight-qt.pro" CONFIG+=enable-pyrowave
make -j4 release
```

Use the normal Qt/SDL/FFmpeg/libplacebo/Vulkan build dependencies. Omit `CONFIG+=enable-pyrowave` for the unchanged dependency-free build. Vulkan 1.3, the PyroWave shader features, and libplacebo are required. Settings offers PyroWave only after a runtime device/decoder probe succeeds.

The decoder borrows libplacebo's VkDevice and uses its queue locking callbacks. Three GPU-local R16 UNORM planes are exchanged using explicit timeline semaphore ownership; libplacebo interprets their native Rec.709 or PQ/BT.2020 metadata and full/limited range and chroma siting. Normal HDR display selection and Sunshine's mastering metadata are reused. There is no decoded-pixel CPU staging.

Enable **HDR** and **YUV 4:4:4** in the existing settings to request them with PyroWave. Both combinations are implemented. Hardware HDR presentation must be verified on an HDR compositor/display; the hidden-window renderer tests only verify GPU encoding, decoding, and rendering.

```sh
./app/moonlight stream --video-codec PyroWave --bitrate 200000 \
  --resolution 3840x2160 --fps 60 --hdr --yuv444 HOST Desktop
```

Use up to 2,000,000 kbps, 240 fps and an encoded frame budget below 3 MiB. Rate/frame combinations outside the budget fail validation. Start with a bitrate appropriate for the LAN and frame size.

## Renderer integration test

After building the normal project dependencies, build the alternate test target from the application build directory:

```sh
cd "$PW_ROOT/build/moonlight-pyrowave/app"
qmake6 "$PW_ROOT/moonlight-qt/app/app.pro" -o Makefile.smoke \
  CONFIG+=enable-pyrowave CONFIG+=pyrowave-smoke-test
make -f Makefile.smoke -j4 release
./pyrowave-render-smoke
SUNSHINE_TEST_PYROWAVE_OUTPUT="$PW_ROOT/build/native-frames" ./pyrowave-render-smoke
```

The first invocation verifies decoder creation, consecutive frames, malformed input, mailbox submission, drops, both sizes and all color/chroma combinations. The second renders production Sunshine DMA-BUF encoder output. These tests need a Vulkan-capable SDL display, even with their hidden window.

Debug logging reports packet completion-to-decode and decode/render submission durations plus periodic GPU timestamps. The mailbox is bounded to one latest frame and all encoded inputs are capped at 3 MiB. Whole-frame FEC remains in use; incomplete frames are discarded and the next intra frame resumes decoding without IDR requests.

## Package a Steam Deck test AppImage

`scripts/package-pyrowave-appimage.py` packages an existing feature-enabled binary plus the renderer diagnostic. This Arch/CachyOS test packager includes a private matching glibc loader/runtime to avoid depending on the Deck having the build machine's glibc. Qt/QML, SDL3 (for SDL2-compat), audio/input backends and the patched PyroWave library are included. GPU drivers remain supplied by SteamOS. This differs from upstream's oldest-distribution AppImage build; actual Deck operation still requires a Deck test.

Use Python 3, binutils, qmake6, an official AppImage packager/runtime, and the existing ON build. Extract the packager with `--appimage-extract` if FUSE is unavailable. Preserve/check the downloaded tool hashes and pass an explicit runtime file so packaging performs no implicit runtime download.

```sh
python3 "$PW_ROOT/moonlight-qt/scripts/package-pyrowave-appimage.py" \
  --binary "$PW_ROOT/build/moonlight-pyrowave/app/moonlight" \
  --smoke-binary "$PW_ROOT/build/moonlight-pyrowave/app/pyrowave-render-smoke" \
  --pyrowave-prefix "$PW_PREFIX" \
  --appdir "$PW_ROOT/build/Moonlight-PyroWave.AppDir" \
  --appimagetool "$PW_ROOT/build/packaging-tools/squashfs-root/usr/bin/appimagetool" \
  --runtime "$PW_ROOT/build/packaging-tools/runtime-x86_64" \
  --output "$PW_ROOT/dist/Moonlight-PyroWave-x86_64.AppImage"
```

Choose a fresh AppDir on each invocation. The package includes license notices and a dependency/runtime manifest. Its private runtime is passed to the loader rather than exported to host child processes.

Copy the AppImage to the Deck, make it executable, and launch it natively. Add it to Steam as a non-Steam game; leave Proton compatibility disabled. If FUSE is unavailable, use `--appimage-extract-and-run`. The included Vulkan diagnostic runs without a Sunshine server:

```sh
./Moonlight-PyroWave-x86_64.AppImage --appimage-extract-and-run --pyrowave-render-test
```

The test prints all SDR/PQ and 420/444 combinations. It does not verify the physical display's HDR luminance. In Settings select PyroWave, then the existing HDR and YUV 4:4:4 preferences. Test HDR on an HDR-capable output/compositor; begin with SDR 4:2:0 if diagnosing initialization.
