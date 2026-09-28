# PyroWave integration

PyroWave is an optional, explicitly selected video codec in Moonlight Qt
PyroWave. Automatic selection continues to use Moonlight's standard codecs.
The client requires both the advertised PyroWave capability bits and the
matching private SDP version marker, so an unrelated or incompatible host is
rejected.

## Dependency and build contract

The release builds pin the patched PyroWave revision
`e344479d6c0439e346c788a918ad5645713f7573`. Its standalone shared C API is
version **0.7.0**. Granite is pinned by PyroWave's `checkout_granite.sh`. Do not
substitute upstream PyroWave 0.6 or an arbitrary newer ABI.

Build and install the dependency without the development applications:

```sh
git clone https://github.com/karsyboy/pyrowave.git deps/PyroWave
git -C deps/PyroWave checkout e344479d6c0439e346c788a918ad5645713f7573
(cd deps/PyroWave && ./checkout_granite.sh)
cmake -S deps/PyroWave -B build/pyrowave \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/pyrowave-install" \
  -DPYROWAVE_DEVEL=OFF \
  -DPYROWAVE_UTILS=OFF
cmake --build build/pyrowave --parallel
cmake --install build/pyrowave
```

On Linux, add `build/pyrowave-install/share/pkgconfig` to `PKG_CONFIG_PATH` and
configure qmake with `CONFIG+=enable-pyrowave`. On Windows, set
`PYROWAVE_PREFIX` to the install prefix and `PYROWAVE_SOURCE` to the source tree
before running `scripts\build-arch.bat`; the script enables the integration and
stages the runtime DLL and license automatically.

The release AppImage is produced by:

```sh
PYROWAVE_SOURCE="$PWD/deps/PyroWave" scripts/build-pyrowave-appimage.sh
```

That script builds PyroWave in the same Ubuntu 22.04 userspace as the client,
then delegates to the existing AppImage packager. The GitHub release workflow
also inspects the completed AppImage and fails unless both the executable's
PyroWave dependency and the bundled shared library are present.

## Runtime integration

The decoder borrows libplacebo's Vulkan instance, device, graphics queue, and
queue-lock callbacks. Three GPU-local R16 UNORM planes are exchanged through
explicit timeline-semaphore ownership. Decode and rendering submissions run on
a dedicated high-priority render thread. It latches the newest encoded frame
and submits its GPU decode before waiting for presentation capacity, allowing
decode work to overlap the swapchain wait without buffering another frame.
libplacebo receives the stream's
native Rec.709 or PQ/BT.2020 metadata, full/limited range, chroma siting, and
Sunshine mastering metadata. Decoded pixels are not staged through CPU memory.

The renderer advertises direct submit because its callback only copies into the
bounded latest-frame mailbox. This bypasses Moonlight's otherwise redundant
15-frame decode-unit queue and avoids bursty handoff from a second decoder
thread. Encoded inputs are capped at 3 MiB and reusable host buffers avoid
steady-state per-frame allocation. Whole-frame FEC remains in use; incomplete
frames are dropped, and the next intra frame resumes decoding without an IDR
request. The client-frame-queue statistic is counted here, before decode, when
a newly reassembled complete encoded frame replaces the pending mailbox frame.

The existing **HDR** and **YUV 4:4:4** preferences select those PyroWave modes.
The codec accepts up to 2,000,000 Kbps and 240 FPS when the computed encoded
frame budget remains between 1 KiB and 3 MiB. Actual HDR output still depends
on an HDR-capable display, compositor, GPU, and driver.

Example CLI selection:

```sh
./app/moonlight stream --video-codec PyroWave --bitrate 200000 \
  --resolution 3840x2160 --fps 60 --hdr --yuv444 HOST Desktop
```

## Renderer smoke test

After building the normal dependencies, generate the alternate test target
from the application build directory:

```sh
cd build/app
qmake6 ../../app/app.pro -o Makefile.smoke \
  CONFIG+=enable-pyrowave CONFIG+=pyrowave-smoke-test
make -f Makefile.smoke -j"$(nproc)" release
./pyrowave-render-smoke
```

The test exercises decoder creation, consecutive and malformed frames, mailbox
replacement, drops, two frame sizes, SDR/PQ, and 4:2:0/4:4:4. It requires a
Vulkan-capable SDL display even though the window is hidden. Providing
`SUNSHINE_TEST_PYROWAVE_OUTPUT` additionally renders compatible frame dumps
from the PyroWave-enabled Sunshine implementation.

The smoke test verifies GPU encoding, decoding, and rendering; it cannot verify
physical HDR luminance or end-to-end network behavior.
