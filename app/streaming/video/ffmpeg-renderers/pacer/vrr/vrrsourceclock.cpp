#include "vrrsourceclock.h"

#include <algorithm>
#include <limits>

namespace Vrr {

namespace {

constexpr uint64_t kQ16One = 1ULL << 16;
// Cadence history bounds: at least this many samples, at most this many.
constexpr size_t kMinimumSamples = 6;
constexpr size_t kMaximumSamples = 512;
// History length: short with spare display headroom, long near the refresh
// ceiling where small rate errors matter.
constexpr uint64_t kLooseWindowUs = 350000;
constexpr uint64_t kTightWindowUs = 1000000;
constexpr uint64_t kLooseHeadroomDisplayPeriods = 2;
// An interval more than 3.5x away from the fitted period is a departure.
constexpr uint64_t kMajorNumerator = 7;
constexpr uint64_t kMajorDenominator = 2;
// A departure is cancelled by an interval within 5:4 of the old period. A
// wider ratio would treat the 16.7 ms half of an alternating 16.7/33.3 ms
// (40 FPS average) source as a return to 60 FPS and never accept 40 FPS.
constexpr uint64_t kReturnNumerator = 5;
constexpr uint64_t kReturnDenominator = 4;
// A provisional rate must hold for three samples spanning 200 ms.
constexpr size_t kCandidateSamples = 3;
constexpr uint64_t kCandidateMinimumUs = 200000;
constexpr uint64_t kMaterialChangePercent = 12;
constexpr uint64_t kMaximumIntervalUs = 1000000;

uint64_t roundedQ16(uint64_t valueQ16)
{
    return (valueQ16 + kQ16One / 2) >> 16;
}

uint64_t periodQ16ForRate(int rateHz)
{
    const uint64_t rate = rateHz > 0 ? static_cast<uint64_t>(rateHz) : 60;
    return (kMicrosecondsPerSecond * kQ16One + rate / 2) / rate;
}

bool withinPercent(uint64_t a, uint64_t b, uint64_t percent)
{
    const uint64_t high = (std::max)(a, b);
    const uint64_t low = (std::min)(a, b);
    return (high - low) * 100 <= high * percent;
}

}

SourceClock::SourceClock(int streamRateHz, uint64_t displayPeriodUs) :
    m_StreamPeriodQ16(periodQ16ForRate(streamRateHz)),
    m_StreamPeriodUs(std::max<uint64_t>(1, roundedQ16(m_StreamPeriodQ16))),
    m_DisplayPeriodUs(std::max<uint64_t>(1, displayPeriodUs)),
    m_SourcePeriodQ16(m_StreamPeriodQ16),
    m_SourcePeriodUs(m_StreamPeriodUs)
{
}

void SourceClock::reset(bool forgetRate)
{
    m_Started = false;
    m_HaveFrameNumber = false;
    m_LastFrameNumber = -1;
    m_LastTimestampValid = false;
    m_LastRtp = 0;
    m_UnwrappedTicks = 0;
    m_Ordinal = 0;
    m_RtpRemainder = 0;
    m_Samples.clear();
    m_Candidate.clear();
    if (forgetRate) {
        m_SourcePeriodQ16 = m_StreamPeriodQ16;
        m_SourcePeriodUs = m_StreamPeriodUs;
    }
}

void SourceClock::start(const FrameTiming& frame)
{
    reset(false);
    m_Started = true;
    m_HaveFrameNumber = frame.frameNumber >= 0;
    m_LastFrameNumber = frame.frameNumber;
    m_LastTimestampValid = frame.timestampValid;
    m_LastRtp = frame.rtpTimestamp;
    m_Samples.push_back({0, 0});
}

uint64_t SourceClock::rtpUs() const
{
    return m_UnwrappedTicks * kMicrosecondsPerSecond / kRtpClockHz;
}

Cadence SourceClock::observe(const FrameTiming& frame)
{
    Cadence cadence;
    if (!m_Started) {
        cadence.rebase = true;
        return cadence;
    }
    if (m_HaveFrameNumber && frame.frameNumber >= 0) {
        if (frame.frameNumber <= m_LastFrameNumber) {
            cadence.rebase = true;
            return cadence;
        }
        cadence.frameDelta = static_cast<uint64_t>(frame.frameNumber - m_LastFrameNumber);
    }

    if (frame.timestampValid && m_LastTimestampValid) {
        const uint32_t delta = frame.rtpTimestamp - m_LastRtp;
        if (delta == 0 || delta > 0x7fffffffU) {
            cadence.rebase = true;
            return cadence;
        }
        const uint64_t numerator = uint64_t(delta) * kMicrosecondsPerSecond + m_RtpRemainder;
        const uint64_t intervalUs = numerator / kRtpClockHz;
        if (intervalUs > kMaximumIntervalUs) {
            cadence.rebase = true;
            return cadence;
        }
        m_RtpRemainder = numerator % kRtpClockHz;
        cadence.intervalUs = intervalUs;
        cadence.usedRtp = true;
        observeRtpCadence(delta, cadence);
    }
    else {
        // Without timestamps only the negotiated rate is known.
        cadence.intervalUs = cadence.frameDelta * m_StreamPeriodUs;
        cadence.eligible = cadence.frameDelta == 1;
        m_Samples.clear();
        m_Candidate.clear();
        m_UnwrappedTicks += cadence.frameDelta * m_StreamPeriodUs * kRtpClockHz / kMicrosecondsPerSecond;
        m_Ordinal += cadence.frameDelta;
        m_Samples.push_back({m_Ordinal, m_UnwrappedTicks});
    }

    m_HaveFrameNumber = frame.frameNumber >= 0;
    m_LastFrameNumber = frame.frameNumber;
    m_LastTimestampValid = frame.timestampValid;
    m_LastRtp = frame.rtpTimestamp;
    return cadence;
}

void SourceClock::observeRtpCadence(uint32_t rtpDelta, Cadence& cadence)
{
    const Sample previous{m_Ordinal, m_UnwrappedTicks};
    m_Ordinal += cadence.frameDelta;
    m_UnwrappedTicks += rtpDelta;
    const Sample current{m_Ordinal, m_UnwrappedTicks};

    if (!m_Candidate.empty()) {
        const uint64_t observed = std::max<uint64_t>(1, cadence.intervalUs / cadence.frameDelta);
        const bool returned =
            observed * kReturnDenominator <= m_SourcePeriodUs * kReturnNumerator &&
            m_SourcePeriodUs * kReturnDenominator <= observed * kReturnNumerator;
        cadence.phaseDiscontinuity = true;
        if (returned) {
            // An isolated gap: keep the rate, restart the cumulative phase.
            m_Candidate.clear();
            m_Samples.clear();
            m_Samples.push_back(current);
            return;
        }
        appendSample(m_Candidate, current);
        const uint64_t spanUs = (m_Candidate.back().ticks - m_Candidate.front().ticks) *
                                kMicrosecondsPerSecond / kRtpClockHz;
        if (m_Candidate.size() >= kCandidateSamples && spanUs >= kCandidateMinimumUs) {
            const uint64_t periodQ16 = fitPeriodQ16(m_Candidate);
            if (periodQ16 != 0) {
                cadence.sourceRateChanged = acceptPeriodQ16(periodQ16);
                m_Samples = m_Candidate;
                m_Candidate.clear();
                cadence.eligible = true;
            }
        }
        return;
    }

    if (majorDeparture(cadence.intervalUs, cadence.frameDelta)) {
        m_Candidate.push_back(previous);
        m_Candidate.push_back(current);
        cadence.phaseDiscontinuity = true;
        return;
    }

    appendSample(m_Samples, current);
    cadence.eligible = true;
    if (m_Samples.size() >= kMinimumSamples) {
        const uint64_t periodQ16 = fitPeriodQ16(m_Samples);
        if (periodQ16 != 0 && acceptPeriodQ16(periodQ16)) {
            cadence.sourceRateChanged = true;
            cadence.phaseDiscontinuity = true;
        }
    }
}

void SourceClock::appendSample(std::deque<Sample>& samples, const Sample& sample) const
{
    samples.push_back(sample);
    while (samples.size() > kMaximumSamples) {
        samples.pop_front();
    }
    const uint64_t windowUs = cadenceWindowUs();
    while (samples.size() > kMinimumSamples) {
        const uint64_t spanUs = (samples.back().ticks - samples.front().ticks) *
                                kMicrosecondsPerSecond / kRtpClockHz;
        if (spanUs <= windowUs) {
            break;
        }
        samples.pop_front();
    }
}

uint64_t SourceClock::fitPeriodQ16(const std::deque<Sample>& samples) const
{
    if (samples.size() < 2) {
        return 0;
    }
    const Sample& first = samples.front();
    const Sample& last = samples.back();
    if (last.ordinal <= first.ordinal || last.ticks <= first.ticks) {
        return 0;
    }
    // The endpoint span measures net source movement. A least-squares slope
    // would "breathe" as the steps of a refresh-quantized host cross the window.
    const uint64_t spanFrames = last.ordinal - first.ordinal;
    const uint64_t spanTicks = last.ticks - first.ticks;
    const uint64_t scale = kMicrosecondsPerSecond * kQ16One;
    if (spanTicks > std::numeric_limits<uint64_t>::max() / scale) {
        return 0;
    }
    const uint64_t numerator = spanTicks * scale;
    const uint64_t denominator = spanFrames * kRtpClockHz;
    return (numerator + denominator / 2) / denominator;
}

bool SourceClock::acceptPeriodQ16(uint64_t periodQ16)
{
    // The negotiated rate bounds the source rate; keep Q16 precision so a
    // fractional rate does not acquire drift from rounding.
    periodQ16 = (std::max)(periodQ16, m_StreamPeriodQ16);
    const uint64_t previousUs = m_SourcePeriodUs;
    m_SourcePeriodQ16 = periodQ16;
    m_SourcePeriodUs = std::max<uint64_t>(1, roundedQ16(periodQ16));
    return !withinPercent(m_SourcePeriodUs, previousUs, kMaterialChangePercent);
}

bool SourceClock::majorDeparture(uint64_t intervalUs, uint64_t frameDelta) const
{
    if (intervalUs == 0 || frameDelta == 0 || m_SourcePeriodUs == 0) {
        return false;
    }
    const uint64_t observed = std::max<uint64_t>(1, intervalUs / frameDelta);
    return observed * kMajorDenominator > m_SourcePeriodUs * kMajorNumerator ||
           m_SourcePeriodUs * kMajorDenominator > observed * kMajorNumerator;
}

uint64_t SourceClock::cadenceWindowUs() const
{
    const uint64_t floorUs = m_DisplayPeriodUs + m_GuardUs;
    const uint64_t headroomUs = m_SourcePeriodUs > floorUs ? m_SourcePeriodUs - floorUs : 0;
    const uint64_t looseUs = m_DisplayPeriodUs * kLooseHeadroomDisplayPeriods;
    if (headroomUs >= looseUs) {
        return kLooseWindowUs;
    }
    if (headroomUs <= m_DisplayPeriodUs) {
        return kTightWindowUs;
    }
    return kLooseWindowUs + (kTightWindowUs - kLooseWindowUs) * (looseUs - headroomUs) /
                                (looseUs - m_DisplayPeriodUs);
}

void OffsetMapper::reset()
{
    m_Window.clear();
    m_Valid = false;
    m_AppliedUs = 0;
    m_Samples = 0;
    m_LastAtUs = 0;
    m_SlewRemainder = 0;
    m_WindowStartUs = 0;
}

int64_t OffsetMapper::observe(uint64_t atUs, int64_t offsetUs, bool eligible, bool phaseDiscontinuity,
                              uint64_t reanchorUs)
{
    constexpr uint64_t kWindowUs = 3000000;
    constexpr uint64_t kWarmupSamples = 64;
    constexpr uint64_t kSlewUsPerSecond = 2400;
    constexpr uint64_t kMaximumStepUs = 100;

    const bool reversed = m_Valid && atUs < m_LastAtUs;
    if (m_Valid && (phaseDiscontinuity || reversed)) {
        m_Window.clear();
        m_Samples = (std::max)(m_Samples, kWarmupSamples);
        m_SlewRemainder = 0;
    }
    if (m_Window.empty()) {
        m_WindowStartUs = atUs;
    }

    // Correction credit accrues with elapsed source time, so the slew rate is
    // independent of the frame rate; a gap buys at most one bounded step.
    uint64_t allowedUs = 0;
    if (m_Valid && atUs > m_LastAtUs) {
        const uint64_t elapsedUs = std::min<uint64_t>(atUs - m_LastAtUs, kMicrosecondsPerSecond);
        const uint64_t credit = elapsedUs * kSlewUsPerSecond + m_SlewRemainder;
        allowedUs = (std::min)(credit / kMicrosecondsPerSecond, kMaximumStepUs);
        m_SlewRemainder = allowedUs < kMaximumStepUs ? credit % kMicrosecondsPerSecond : 0;
    }
    m_LastAtUs = atUs;

    if (m_Valid && (!eligible || phaseDiscontinuity)) {
        // A transition frame never teaches the floor.
        m_SlewRemainder = 0;
        return m_AppliedUs;
    }

    while (!m_Window.empty() && m_Window.back().offsetUs >= offsetUs) {
        m_Window.pop_back();
    }
    m_Window.push_back({atUs, offsetUs});
    while (m_Window.size() > 1 && atUs > kWindowUs && m_Window.front().atUs < atUs - kWindowUs) {
        m_Window.pop_front();
    }
    const int64_t minimumUs = m_Window.front().offsetUs;

    ++m_Samples;
    if (!m_Valid) {
        m_AppliedUs = offsetUs;
        m_Valid = true;
    }
    else if (m_Samples <= kWarmupSamples) {
        m_AppliedUs = (std::min)(m_AppliedUs, minimumUs);
    }
    else if (reanchorUs && minimumUs > m_AppliedUs + int64_t(reanchorUs) &&
             atUs - m_WindowStartUs >= kMicrosecondsPerSecond) {
        m_AppliedUs = minimumUs;
        ++m_Reanchors;
    }
    else if (minimumUs > m_AppliedUs) {
        m_AppliedUs += std::min<int64_t>(int64_t(allowedUs), minimumUs - m_AppliedUs);
    }
    else if (minimumUs < m_AppliedUs) {
        m_AppliedUs -= std::min<int64_t>(int64_t(allowedUs), m_AppliedUs - minimumUs);
    }
    if (m_AppliedUs == minimumUs) {
        m_SlewRemainder = 0;
    }
    return m_AppliedUs;
}

}
