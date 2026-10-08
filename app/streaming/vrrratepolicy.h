#pragma once

#include <vector>

// Stream-rate choices and session admission for VRR presentation. No SDL,
// Qt or renderer dependency, so the arithmetic is unit tested directly.
enum class VrrFpsChoiceKind {
    Fixed,
    Vrr,
    LowLatencyVrr,
    Custom,
};

struct VrrFpsChoice {
    int fps;
    VrrFpsChoiceKind kind;
};

namespace VrrRatePolicy {

// Recommended VRR stream rate below a refresh rate: floor(r - r^2 / 3600)
// (116 at 120 Hz, 138 at 144 Hz, 157 at 165 Hz, 224 at 240 Hz). The margin
// grows with the refresh rate, so a source running slightly fast or a
// late-then-early frame pair still lands inside the panel's adaptive range
// instead of waiting for the next fixed refresh.
int vrrRateForRefresh(int refreshHz);

// Lower-latency choice: floor(r / 6) * 5 (100 at 120 Hz, 120 at 144 Hz). A
// sixth of the panel's range is left as headroom, so frames that arrive late
// can be shown sooner without being held for spacing.
int lowLatencyRateForRefresh(int refreshHz);

// A stream up to the native refresh rate is admitted; per-frame presentation
// protection handles spacing near the ceiling.
bool hasAdaptiveHeadroom(int streamRateHz, int displayRefreshHz);

// Fixed 30/60, every native refresh rate, the two VRR choices per refresh
// rate when VRR is enabled, and a saved custom value. Native rates win over
// a coincident calculated rate from another display. Sorted by FPS.
std::vector<VrrFpsChoice> buildChoices(const std::vector<int>& refreshRates,
                                       int savedFps,
                                       bool vrrEnabled);

}
