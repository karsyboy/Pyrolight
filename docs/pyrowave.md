# PyroWave integration

PyroWave is an optional, explicitly selected video codec in Moonlight Qt
PyroWave. Automatic selection continues to use Moonlight's standard codecs.
The client requires advertised PyroWave profile bits and an explicit supported
transport contract. Native version 1 is preferred; verified Nonary/Vibepollo
record framing is selected only through setup negotiation. Unknown bitstream
families or contradictory dialect advertisements are rejected with a launch
error. See [networking and compatibility](pyrowave-networking.md).

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
host mastering metadata. Decoded pixels are not staged through CPU memory.
Software Vulkan devices are rejected explicitly: PyroWave requires a hardware
Vulkan GPU and never falls back to CPU decoding or another codec.
The integration queries PyroWave's device preference and uses its fragment
decode path on mobile-class GPUs that perform poorly with the compute iDWT;
desktop-class GPUs retain the compute path.
For device-specific benchmarking, `PYROWAVE_FRAGMENT_PATH=1` forces fragment
decode and `PYROWAVE_FRAGMENT_PATH=0` forces compute decode. Without the
variable, PyroWave's device recommendation is used.
The debug overlay reports PyroWave's aggregated GPU dequantization and iDWT
timestamps separately from CPU decode submission and presentation waiting.

The renderer advertises direct submit because its callback only copies into the
bounded latest-frame mailbox. This bypasses Moonlight's otherwise redundant
15-frame decode-unit queue and avoids bursty handoff from a second decoder
thread. Encoded inputs are capped at 3 MiB and reusable host buffers avoid
steady-state per-frame allocation. Native whole-frame FEC remains in use; incomplete
native frames are dropped, and the next intra frame resumes decoding without an IDR
request. The client-frame-queue statistic is counted here, before decode, when
a newly reassembled complete encoded frame replaces the pending mailbox frame.

The existing **HDR** and **YUV 4:4:4** preferences select those PyroWave modes.
The codec accepts up to 2,000,000 Kbps and 240 FPS when the computed encoded
frame budget remains between 1 KiB and 3 MiB. Actual HDR output still depends
on an HDR-capable display, compositor, GPU, and driver.

PyroWave uses a codec-specific default because it is intra-only. The default
starts at 400,000 bytes per 3840x2160 4:2:0 SDR frame, scales with pixel count,
applies the codec evaluation's conservative 20% allowances for 4:4:4 and HDR,
and then multiplies by FPS. Thus 4K defaults are approximately 192 Mbps at 60 FPS, 384 Mbps at 120
FPS, and 461 Mbps at 144 FPS before 4:4:4/HDR scaling. These are tuning seeds,
not guaranteed requirements. Selecting a bitrate manually clears default
tracking; Moonlight will not silently replace it. `autoAdjustBitrate` only
tracks setting changes and is not a live network bitrate controller.

The bitstream carries the visible width and height. PyroWave's 32-pixel wavelet
alignment remains internal to the codec; the decoder's libplacebo textures and
crop use the true visible dimensions. This prevents padded rows or columns from
being rescaled into the displayed picture.

V-Sync is not changed or forced by the PyroWave decoder. With V-Sync enabled,
Moonlight keeps its synchronized presentation behavior. With V-Sync disabled,
the Vulkan renderer selects the lowest-latency supported present mode. If a
high-refresh stream is unexpectedly presentation-limited, testing with V-Sync
disabled is a useful diagnostic, but it is not universally required.

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
from a compatible Pyroshine implementation. The environment variable retains
its historical name for compatibility.

The smoke test verifies GPU encoding, decoding, and rendering; it cannot verify
physical HDR luminance or end-to-end network behavior.


### Audited dependency patches and SDR transfer

The pinned PyroWave/Granite sources require the patches in
`scripts/pyrowave-patches/`: full-resolution 4:4:4 payload allocation and
200-nit normalization when converting PQ/scRGB into SDR. The AppImage builder
and Windows release workflow apply these patches. For manual library builds,
apply them from the PyroWave source root after `./checkout_granite.sh` and before
CMake. Do not change the source pins independently on the host and client.

Wire version 1 SDR pixels use the scaler's sRGB transfer function with BT.709
primaries and matrix. Its bitstream SDR/PQ transfer bit alone cannot distinguish
sRGB from other SDR transfer functions. The native renderer therefore uses
libplacebo's sRGB transfer for this protocol. Decoded planes are normalized
floats stored in R16_UNORM: their libplacebo sample and color depths must both
be 8 for SDR, or both 10 for HDR, without a 65535/255 or 65535/1023 rescale.
Ten-bit SDR and limited-range streams remain unsupported by this host protocol.
