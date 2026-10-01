#pragma once
#include <algorithm>
#include <cstdint>
#include <array>
#include <limits>
namespace PyroWaveBandwidth {
constexpr std::int64_t ProbeBytes = 32LL * 1024 * 1024;
// decimal kbit/s; use nanosecond timing and 64-bit multiplication.
inline std::int64_t throughputKbps(std::int64_t bytes, std::int64_t ns) {
    if (bytes != ProbeBytes || ns <= 0) return 0;
    return bytes * 8000000LL / ns;
}
inline std::int64_t ceilingKbps(const std::array<std::int64_t, 3>& samples,
                               std::int64_t hostMbps, std::int64_t clientMbps) {
    auto available = (std::min)({samples[0], samples[1], samples[2]});
    if (available <= 0) return 0;
    for (auto speed : {hostMbps, clientMbps}) {
        if (speed > 0 && speed <= (std::numeric_limits<std::int64_t>::max)() / 1000)
            available = (std::min)(available, speed * 1000);
    }
    // Division before multiplication avoids overflow without losing >1 kbps.
    return available / 5 * 4;
}
}
