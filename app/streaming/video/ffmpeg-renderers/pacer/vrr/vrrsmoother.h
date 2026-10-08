#pragma once

#include "vrrsourceclock.h"

#include <algorithm>
#include <array>

namespace Vrr {

// "Reduce judder": cadence smoothing of source timestamps.
//
// A host that samples a game on its own clock produces adjacent short/long
// intervals (for example 7/13 ms around a 10 ms average). Presenting at the
// raw timestamps reproduces that unevenness on a VRR panel. The smoother
// tracks the source period, predicts each slot from the previous smoothed
// slot, and places the frame 85 % of the way toward the prediction:
//
//   trackedPeriod += 0.025 * (interval - trackedPeriod)
//   error          = (lastSlot + trackedPeriod) - (rawSlot + reserve)
//   trackedPeriod -= 0.02 * error                (phase feedback)
//   retiming       = clamp(0.85 * error, -(delay + reserve), maxLag - reserve)
//
// It trades timestamp fidelity for steadier motion within a 6 ms positive
// allowance that the playout buffer's capacity already reserves. It resets
// on rebases, rate changes, phase discontinuities, stalls and bursts; it is
// not a fixed-rate generator. With the readiness bound, advancing a frame
// never makes it present before it can be ready, and the lateness that such
// an advance would cause is covered by a small learned reserve instead of
// the interval buffer.
class CadenceSmoother {
public:
    static constexpr int64_t kMaxLagUs = 6000;
    static constexpr uint64_t kReserveMaxUs = 3000;

    void reset()
    {
        resetCadence();
        m_TrackedPeriodUs = 0;
        m_PeriodRemainder = 0;
        m_FeedbackRemainder = 0;
        m_IntervalCount = 0;
        m_IntervalIndex = 0;
        m_PreviousIntervalUs = 0;
        m_LastRetimingUs = 0;
        m_Shortfalls = {};
        m_ShortfallCount = 0;
        m_ShortfallIndex = 0;
        m_ReserveUs = 0;
        m_ReserveRemainder = 0;
        m_LastReserveUpdateUs = 0;
        m_Engaged = false;
    }

    // Forget the smoothed basis (the next frame presents on its raw slot).
    void resetCadence()
    {
        m_HaveBasis = false;
        m_BasisUs = 0;
    }

    uint64_t reserveUs() const { return m_ReserveUs; }
    bool engaged() const { return m_Engaged; }

    struct Result {
        // Applied retiming relative to the raw slot, including the reserve.
        int64_t smoothingUs = 0;
        // Retiming the smoother asked for before the readiness bound.
        int64_t requestedUs = 0;
        // How far the readiness bound moved the request later.
        uint64_t readinessPushUs = 0;
    };

    // `rawBasisUs` is sourceTime + delay; `readyOffsetUs` is the frame's
    // readiness relative to its source time.
    Result adjust(const Cadence& cadence, bool rebased, uint64_t sourcePeriodUs,
                  uint64_t rawBasisUs, uint64_t delayUs, int64_t readyOffsetUs)
    {
        m_Engaged = false;
        Result result;
        const int64_t reserve = int64_t(m_ReserveUs);
        int64_t retiming = cadenceAdjust(cadence, rebased, sourcePeriodUs, rawBasisUs + m_ReserveUs, delayUs);
        if (!m_Engaged && !rebased) {
            // Ease the retiming the previous frame carried back to the raw
            // slot instead of stepping it into one presented interval.
            constexpr int64_t kResetSlewUs = 1000;
            retiming = m_LastRetimingUs > kResetSlewUs ? m_LastRetimingUs - kResetSlewUs :
                       m_LastRetimingUs < -kResetSlewUs ? m_LastRetimingUs + kResetSlewUs : 0;
            retiming = (std::min)(retiming, kMaxLagUs - reserve);
            retiming = (std::max)(retiming, -(int64_t(delayUs) + reserve));
        }
        result.requestedUs = retiming;
        // Spend only the time left before the raw slot, keeping known readiness.
        const int64_t readyFloor = std::min<int64_t>(0, readyOffsetUs - int64_t(delayUs));
        const int64_t bounded = (std::max)(retiming, readyFloor - reserve);
        result.readinessPushUs = uint64_t(bounded - retiming);
        m_LastRetimingUs = bounded;
        result.smoothingUs = bounded + reserve;
        return result;
    }

    // Record the slot this frame was scheduled at (without readiness push),
    // so a late frame cannot move later frames.
    void commitBasis(uint64_t basisUs)
    {
        m_BasisUs = basisUs;
        m_HaveBasis = true;
    }

    // Learn the reserve from the lateness the smoother itself caused: the
    // p98 of the last 128 placed frames' shortfall minus 500 us, at most
    // 3 ms, +250 us per frame, released at 500 us/s.
    void observeReserve(bool engaged, int64_t shortfallUs, uint64_t nowUs)
    {
        constexpr uint64_t kAttackUs = 250;
        constexpr size_t kMinimumSamples = 32;
        constexpr int64_t kToleranceUs = 500;
        constexpr uint64_t kReleaseUsPerSecond = 500;
        const uint64_t elapsedUs = m_LastReserveUpdateUs && nowUs > m_LastReserveUpdateUs ?
            std::min<uint64_t>(nowUs - m_LastReserveUpdateUs, 100000) : 0;
        m_LastReserveUpdateUs = nowUs;
        uint64_t desired = 0;
        if (engaged) {
            m_Shortfalls[m_ShortfallIndex] = shortfallUs;
            m_ShortfallIndex = (m_ShortfallIndex + 1) % m_Shortfalls.size();
            m_ShortfallCount = (std::min)(m_ShortfallCount + 1, m_Shortfalls.size());
            if (m_ShortfallCount >= kMinimumSamples) {
                std::array<int64_t, 128> ordered;
                std::copy_n(m_Shortfalls.begin(), m_ShortfallCount, ordered.begin());
                const size_t rank = std::max<size_t>(1, (m_ShortfallCount * 980 + 999) / 1000);
                std::nth_element(ordered.begin(), ordered.begin() + (rank - 1), ordered.begin() + m_ShortfallCount);
                const int64_t quantile = ordered[rank - 1];
                desired = quantile > kToleranceUs ?
                    std::min<uint64_t>(uint64_t(quantile - kToleranceUs), kReserveMaxUs) : 0;
            }
            else {
                desired = m_ReserveUs;
            }
        }
        if (desired > m_ReserveUs) {
            m_ReserveUs = (std::min)(desired, m_ReserveUs + kAttackUs);
            m_ReserveRemainder = 0;
        }
        else if (desired < m_ReserveUs) {
            const uint64_t numerator = kReleaseUsPerSecond * elapsedUs + m_ReserveRemainder;
            m_ReserveRemainder = numerator % 1000000;
            m_ReserveUs -= (std::min)(m_ReserveUs - desired, numerator / 1000000);
        }
    }

private:
    static bool withinPercent(uint64_t a, uint64_t b, uint64_t percent)
    {
        const uint64_t high = (std::max)(a, b), low = (std::min)(a, b);
        return (high - low) * 100 <= high * percent;
    }

    int64_t cadenceAdjust(const Cadence& cadence, bool rebased, uint64_t periodUs,
                          uint64_t rawBasisUs, uint64_t delayUs)
    {
        if (periodUs == 0) {
            resetCadence();
            return 0;
        }
        // A 120 FPS half-period interval alternates 4166/4167 us after
        // conversion: never treat that rounding as a burst.
        constexpr uint64_t kRtpQuantumUs = (kMicrosecondsPerSecond + kRtpClockHz - 1) / kRtpClockHz;
        const uint64_t interval = cadence.intervalUs;
        const bool compensatedShort = interval * 4 >= periodUs && m_PreviousIntervalUs > periodUs &&
            interval < periodUs && withinPercent((interval + m_PreviousIntervalUs) / 2, periodUs, 25);
        const bool bounded = interval <= periodUs * 5 / 2 &&
            (interval * 2 + kRtpQuantumUs >= periodUs || compensatedShort);
        const bool continuous = !rebased && !cadence.sourceRateChanged && !cadence.phaseDiscontinuity &&
            cadence.eligible && cadence.frameDelta == 1 && interval != 0 && bounded;

        // Qualify cadence over four intervals, not the side of a rounding
        // boundary one timestamp lands on.
        bool stable = false;
        if (!continuous) {
            m_IntervalCount = 0;
            m_IntervalIndex = 0;
        }
        else {
            m_Intervals[m_IntervalIndex] = interval;
            m_IntervalIndex = (m_IntervalIndex + 1) % m_Intervals.size();
            m_IntervalCount = (std::min)(m_IntervalCount + 1, m_Intervals.size());
            uint64_t total = 0;
            for (size_t i = 0; i < m_IntervalCount; ++i) {
                total += m_Intervals[i];
            }
            stable = m_IntervalCount == m_Intervals.size() &&
                     withinPercent(total, periodUs * m_IntervalCount, 25);
        }
        m_PreviousIntervalUs = cadence.eligible && cadence.frameDelta == 1 && !rebased &&
                               !cadence.phaseDiscontinuity ? interval : 0;
        if (!stable) {
            resetCadence();
            return 0;
        }

        // The cumulative rate fit is the authority; reseed the tracked period
        // when they disagree by more than a quarter.
        if (m_TrackedPeriodUs == 0 || m_TrackedPeriodUs > periodUs + periodUs / 4 ||
            m_TrackedPeriodUs + periodUs / 4 < periodUs) {
            m_TrackedPeriodUs = periodUs;
            m_PeriodRemainder = 0;
            m_FeedbackRemainder = 0;
        }
        const int64_t update = (int64_t(interval) - int64_t(m_TrackedPeriodUs)) * 25 + m_PeriodRemainder;
        m_TrackedPeriodUs = uint64_t(std::max<int64_t>(1, int64_t(m_TrackedPeriodUs) + update / 1000));
        m_PeriodRemainder = update % 1000;

        if (!m_HaveBasis) {
            return 0;
        }
        const int64_t error = int64_t(m_BasisUs + m_TrackedPeriodUs) - int64_t(rawBasisUs);
        // More than three periods away is a new phase, not judder.
        if (error > int64_t(3 * m_TrackedPeriodUs) || error < -int64_t(3 * m_TrackedPeriodUs)) {
            resetCadence();
            return 0;
        }
        // Integrate the phase error into the period so a drifting game rate
        // does not leave a standing offset from the raw slots.
        const int64_t feedback = -error * 20000 + m_FeedbackRemainder;
        m_TrackedPeriodUs = uint64_t(std::max<int64_t>(1, int64_t(m_TrackedPeriodUs) + feedback / 1000000));
        m_FeedbackRemainder = feedback % 1000000;

        int64_t adjust = error * 850 / 1000;
        adjust = (std::min)(adjust, kMaxLagUs - int64_t(m_ReserveUs));
        adjust = (std::max)(adjust, -int64_t(delayUs + m_ReserveUs));
        m_Engaged = true;
        return adjust;
    }

    bool m_HaveBasis = false;
    uint64_t m_BasisUs = 0;
    uint64_t m_TrackedPeriodUs = 0;
    int64_t m_PeriodRemainder = 0;
    int64_t m_FeedbackRemainder = 0;
    std::array<uint64_t, 4> m_Intervals{};
    size_t m_IntervalCount = 0;
    size_t m_IntervalIndex = 0;
    uint64_t m_PreviousIntervalUs = 0;
    int64_t m_LastRetimingUs = 0;
    std::array<int64_t, 128> m_Shortfalls{};
    size_t m_ShortfallCount = 0;
    size_t m_ShortfallIndex = 0;
    uint64_t m_ReserveUs = 0;
    uint64_t m_ReserveRemainder = 0;
    uint64_t m_LastReserveUpdateUs = 0;
    bool m_Engaged = false;
};

}
