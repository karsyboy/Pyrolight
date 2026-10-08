#pragma once

#include "vrrtypes.h"

#include <deque>

namespace Vrr {

// How one frame relates to the source timeline.
struct Cadence {
    // The timeline cannot continue (frame number reset, RTP moved backwards
    // or jumped more than a second): restart it at this frame.
    bool rebase = false;
    uint64_t frameDelta = 1;
    // Source interval since the previous frame, from RTP when available.
    uint64_t intervalUs = 0;
    bool usedRtp = false;
    // A steady sample of the current cadence.
    bool eligible = false;
    // A gap or a provisional rate change: the phase of this frame is new.
    bool phaseDiscontinuity = false;
    // The fitted source rate changed materially.
    bool sourceRateChanged = false;
};

// The host's frame timeline, learned from RTP timestamps.
//
// The source period is fitted from the endpoint span of recent RTP times
// divided by their frame-number span: frames lost on the network or dropped
// locally do not turn into an apparently slower source, and a host whose
// capture is quantized to a refresh grid still yields its average rate. The
// negotiated stream rate is the fastest admissible source. A single long gap
// (a hitch, a loading screen) does not change the rate: a large departure
// starts a provisional candidate that only replaces the fitted rate once it
// persists.
class SourceClock {
public:
    SourceClock(int streamRateHz, uint64_t displayPeriodUs);

    // Forget the timeline. The fitted rate is kept unless `forgetRate`.
    void reset(bool forgetRate);
    bool started() const { return m_Started; }
    // Begin a new timeline at `frame`.
    void start(const FrameTiming& frame);
    // Classify `frame` and advance the timeline when it continues.
    Cadence observe(const FrameTiming& frame);

    // The current frame on the unwrapped RTP timeline (0 at start()).
    uint64_t rtpUs() const;
    uint64_t sourcePeriodUs() const { return m_SourcePeriodUs; }
    uint64_t streamPeriodUs() const { return m_StreamPeriodUs; }
    void setGuardUs(uint64_t guardUs) { m_GuardUs = guardUs; }

private:
    struct Sample {
        uint64_t ordinal;
        uint64_t ticks;
    };

    void observeRtpCadence(uint32_t rtpDelta, Cadence& cadence);
    void appendSample(std::deque<Sample>& samples, const Sample& sample) const;
    uint64_t fitPeriodQ16(const std::deque<Sample>& samples) const;
    bool acceptPeriodQ16(uint64_t periodQ16);
    bool majorDeparture(uint64_t intervalUs, uint64_t frameDelta) const;
    uint64_t cadenceWindowUs() const;

    const uint64_t m_StreamPeriodQ16;
    const uint64_t m_StreamPeriodUs;
    const uint64_t m_DisplayPeriodUs;
    uint64_t m_GuardUs = 100;

    bool m_Started = false;
    bool m_HaveFrameNumber = false;
    int64_t m_LastFrameNumber = -1;
    bool m_LastTimestampValid = false;
    uint32_t m_LastRtp = 0;
    uint64_t m_UnwrappedTicks = 0;
    uint64_t m_Ordinal = 0;
    uint64_t m_RtpRemainder = 0;

    uint64_t m_SourcePeriodQ16;
    uint64_t m_SourcePeriodUs;
    std::deque<Sample> m_Samples;
    std::deque<Sample> m_Candidate;
};

// Maps the source timeline onto the client clock: the windowed minimum of
// (ready time - RTP time) over three seconds, i.e. the earliest arrival of
// the recent past. While an epoch warms up a lower minimum is adopted at
// once; afterwards the applied offset slews toward the window minimum at a
// bounded rate, far above clock drift but too slow to be visible per frame.
// A phase discontinuity drops the window (an old phase must not steer the
// new one) but keeps the applied offset, so nothing jumps.
//
// When every frame of at least a second maps later than its slot by more
// than `reanchorUs` (the playout delay plus a source period), the path
// latency stepped up (a route change, a renegotiated link): the offset moves
// to the window minimum at once instead of presenting every frame on arrival
// for the minutes a bounded slew would take. Moving later only lengthens one
// interval; it never compresses presents.
class OffsetMapper {
public:
    void reset();
    bool valid() const { return m_Valid; }
    int64_t offsetUs() const { return m_AppliedUs; }
    uint64_t reanchors() const { return m_Reanchors; }
    // `atUs` is the observation's position on the source timeline.
    int64_t observe(uint64_t atUs, int64_t offsetUs, bool eligible, bool phaseDiscontinuity,
                    uint64_t reanchorUs = 0);

private:
    struct Sample {
        uint64_t atUs;
        int64_t offsetUs;
    };
    // Ascending offsets, increasing time: the front is the window minimum.
    std::deque<Sample> m_Window;
    bool m_Valid = false;
    int64_t m_AppliedUs = 0;
    uint64_t m_Samples = 0;
    uint64_t m_LastAtUs = 0;
    uint64_t m_SlewRemainder = 0;
    uint64_t m_WindowStartUs = 0;
    uint64_t m_Reanchors = 0;
};

}
