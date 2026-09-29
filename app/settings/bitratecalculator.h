#pragma once

#include <cstdint>
#include <limits>

namespace BitrateCalculator {

constexpr std::uint64_t PyroWaveBaseWidth = 3840;
constexpr std::uint64_t PyroWaveBaseHeight = 2160;
constexpr std::uint64_t PyroWaveBase420SdrBytesPerFrame = 400000;
constexpr std::uint64_t PyroWaveWireV1MaxFrameBytes = 3 * 1024 * 1024 - 8;
constexpr std::uint64_t PyroWaveMaxBitrateKbps = 2000000;

// PyroWave is intra-only. Keep its default frame budget independent of FPS,
// then turn that budget into a bitrate by multiplying by the requested FPS.
constexpr int pyroWaveDefaultKbps(int width, int height, int fps, bool yuv444, bool hdr)
{
    if (width <= 0 || height <= 0 || fps <= 0) return 500;
    const std::uint64_t pixels = std::uint64_t(width) * std::uint64_t(height);
    const std::uint64_t numerator = pixels > std::numeric_limits<std::uint64_t>::max() /
                                             PyroWaveBase420SdrBytesPerFrame ?
                                    std::numeric_limits<std::uint64_t>::max() :
                                    pixels * PyroWaveBase420SdrBytesPerFrame;
    std::uint64_t bytes = numerator / (PyroWaveBaseWidth * PyroWaveBaseHeight);
    // Clamp before format scaling too, keeping all following multiplication safe.
    if (bytes > PyroWaveWireV1MaxFrameBytes) bytes = PyroWaveWireV1MaxFrameBytes;
    // PyroWave's checked-in objective/subjective evaluation found roughly a
    // 15-20% 4:4:4 penalty at equal luminance quality, despite 2x raw chroma.
    if (yuv444) bytes = bytes * 6 / 5;
    // The codec's preliminary HDR evaluation uses the same ~20% allowance.
    if (hdr) bytes = bytes * 6 / 5;
    bytes &= ~std::uint64_t(3);
    if (bytes < 1024) bytes = 1024;
    if (bytes > PyroWaveWireV1MaxFrameBytes) bytes = PyroWaveWireV1MaxFrameBytes;
    const std::uint64_t kbps = bytes * std::uint64_t(fps) * 8 / 1000;
    return int(kbps > PyroWaveMaxBitrateKbps ? PyroWaveMaxBitrateKbps : kbps);
}

static_assert(pyroWaveDefaultKbps(1920, 1080, 60, false, false) == 48000);
static_assert(pyroWaveDefaultKbps(1920, 1080, 120, false, false) == 96000);
static_assert(pyroWaveDefaultKbps(2560, 1440, 120, false, false) >
              pyroWaveDefaultKbps(1920, 1080, 120, false, false));
static_assert(pyroWaveDefaultKbps(2560, 1440, 120, false, false) ==
              2 * pyroWaveDefaultKbps(2560, 1440, 60, false, false));
static_assert(pyroWaveDefaultKbps(3440, 1440, 120, false, false) ==
              2 * pyroWaveDefaultKbps(3440, 1440, 60, false, false));
static_assert(pyroWaveDefaultKbps(3840, 2160, 60, false, false) == 192000);
static_assert(pyroWaveDefaultKbps(3840, 2160, 120, false, false) == 384000);
static_assert(pyroWaveDefaultKbps(3840, 2160, 144, false, false) == 460800);
static_assert(pyroWaveDefaultKbps(3840, 2160, 240, false, false) == 768000);
static_assert(pyroWaveDefaultKbps(3840, 2160, 60, true, false) == 230400);
static_assert(pyroWaveDefaultKbps(3840, 2160, 60, false, true) == 230400);

} // namespace BitrateCalculator
