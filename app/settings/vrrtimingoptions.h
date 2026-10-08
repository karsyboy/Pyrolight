#pragma once

#include <algorithm>

// User-visible VRR timing controls. Each preset only chooses these four
// values; admission, release and late-frame recovery are shared policy.
//
// - bufferPerMille: largest playout buffer, in thousandths of the fitted
//   source frame period (500 = half a frame).
// - targetHundredths: share of presented intervals that must stay within the
//   tolerance before the buffer may grow, in hundredths of a percent
//   (9950 = 99.50 %).
// - historySeconds: window over which that share is measured.
// - toleranceUs: interval error that still counts as on time.
//
// Zero means "use the selected preset" so callers without saved custom values
// keep following preset changes.
struct VrrTimingOptions {
    int bufferPerMille = 0;
    int targetHundredths = 0;
    int historySeconds = 0;
    int toleranceUs = 0;

    // Mode IDs are persisted: 0 Smoothest, 1 Balanced, 2 Lowest latency.
    static VrrTimingOptions preset(int mode)
    {
        switch (mode) {
        case 0:
            return VrrTimingOptions{4000, 9999, 300, 250};
        case 2:
            return VrrTimingOptions{500, 9900, 60, 500};
        default:
            return VrrTimingOptions{1000, 9950, 120, 500};
        }
    }

    VrrTimingOptions resolved(int mode) const
    {
        const VrrTimingOptions defaults = preset(mode);
        const auto pick = [](int value, int fallback, int low, int high) {
            return (std::max)(low, (std::min)(high, value == 0 ? fallback : value));
        };
        const int tolerance = pick(toleranceUs, defaults.toleranceUs, 250, 2000);
        return VrrTimingOptions{
            pick(bufferPerMille, defaults.bufferPerMille, 250, 4000),
            pick(targetHundredths, defaults.targetHundredths, 9000, 9999),
            pick(historySeconds, defaults.historySeconds, 10, 300),
            ((tolerance + 125) / 250) * 250,
        };
    }

    bool operator==(const VrrTimingOptions& other) const
    {
        return bufferPerMille == other.bufferPerMille &&
               targetHundredths == other.targetHundredths &&
               historySeconds == other.historySeconds &&
               toleranceUs == other.toleranceUs;
    }
};
