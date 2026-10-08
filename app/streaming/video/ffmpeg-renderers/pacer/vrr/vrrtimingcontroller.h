#pragma once

#include "vrrintervalbuffer.h"
#include "vrrsmoother.h"
#include "vrrsourceclock.h"
#include "vrrtypes.h"

#include <cstdint>
#include <deque>

namespace Vrr {

// Decides when each frame is presented on a VRR display.
//
// target = sourceTime + smoothing + playoutDelay + typicalPreparation
//
// - sourceTime maps the frame's RTP time onto the client clock through the
//   earliest recent arrival (OffsetMapper), so network, decode and capture
//   jitter do not move the schedule; the playout delay is the jitter budget.
// - smoothing ("Reduce judder") regularizes uneven source spacing.
// - playoutDelay is the interval-quality buffer, bounded by the preset's
//   share of a source frame and by the worker's queue capacity.
//
// A late frame is clamped to "now" and the next frame returns to its own
// slot: no lateness is carried forward. After a late present, recovery
// spreads the catch-up over the available display headroom instead of
// presenting the next frames back to back. Frames whose slot is closer than
// one display period to the previous present are latched (synchronized to
// the next refresh) when the presenter can do that natively; otherwise a
// software floor of one display period plus a guard applies.
class TimingController {
public:
    struct Stats {
        uint64_t sourcePeriodUs = 0;
        uint64_t playoutDelayUs = 0;
        uint64_t requestedDelayUs = 0;
        uint64_t maximumDelayUs = 0;
        uint64_t renderLeadUs = 0;
        uint64_t smoothingReserveUs = 0;
        double intervalQualityPercent = 100.0;
        uint64_t intervals = 0;
        uint64_t failedIntervals = 0;
        IntervalBuffer::Action bufferAction = IntervalBuffer::Action::Learning;
        bool calibrated = false;
        uint64_t scheduled = 0;
        uint64_t presented = 0;
        uint64_t latched = 0;
        uint64_t floorDelayed = 0;
        uint64_t catchUps = 0;
        // Presents more than a millisecond after their intended target.
        uint64_t lateTargets = 0;
        uint64_t rebases = 0;
        uint64_t rateChanges = 0;
        uint64_t phaseResets = 0;
    };

    explicit TimingController(const SessionConfig& config);

    // Forget all learned state (a new stream epoch).
    void reset();

    Decision schedule(const FrameTiming& frame, uint64_t nowUs);
    // The software spacing floor for the decision just made; 0 if none.
    uint64_t earliestSubmissionUs() const;
    void noteSubmission(const Submission& submission);

    // A frame older than this, with a newer frame waiting behind it, is stale
    // and replaced. Never less than two source intervals, and never less than
    // what an on-time frame deliberately spends: the playout delay plus its
    // preparation, which starts after the frame leaves the queue.
    uint64_t staleHorizonUs(uint64_t successorIntervalUs) const;

    uint64_t displayPeriodUs() const { return m_DisplayPeriodUs; }
    uint64_t sourcePeriodUs() const { return m_Clock.sourcePeriodUs(); }
    uint64_t playoutDelayUs() const { return m_DelayUs; }
    const SessionConfig& config() const { return m_Config; }
    Stats stats() const;

private:
    struct Pending {
        bool valid = false;
        int64_t frameNumber = -1;
        uint64_t intendedTargetUs = 0;
        uint64_t intervalIntendedUs = 0;
        uint64_t deadlineUs = 0;
        uint64_t appliedDelayUs = 0;
        bool intervalValid = false;
        bool absorbable = false;
        bool latched = false;
    };

    void rebase();
    void updateDelay();
    uint64_t delayMinimumUs() const;
    uint64_t delayMaximumUs() const;
    uint64_t queueLimitUs() const;
    uint64_t typicalPreparationUs() const;
    void learnPreparation(uint64_t durationUs);
    void noteRateEpoch(uint64_t atUs);

    const SessionConfig m_Config;
    const uint64_t m_DisplayPeriodUs;
    const uint64_t m_GuardUs;

    SourceClock m_Clock;
    OffsetMapper m_Offset;
    IntervalBuffer m_Buffer;
    CadenceSmoother m_Smoother;

    uint64_t m_DelayUs = 0;
    bool m_DelayInitialized = false;
    uint64_t m_FutureProjections = 0;

    bool m_HaveLastSubmission = false;
    uint64_t m_LastSubmissionUs = 0;
    uint64_t m_SpacingAnchorUs = 0;
    bool m_Latched = false;
    bool m_CatchUpActive = false;

    std::deque<uint64_t> m_Preparations;
    bool m_FirstPreparationSeen = false;
    uint64_t m_RenderLeadUs = 0;
    uint64_t m_TypicalPreparationUs = 0;

    // Delay learned per source-rate band, restored (downward only) when a
    // known rate returns, e.g. a game alternating menus and gameplay.
    uint64_t m_EpochRateMilliHz = 0;
    uint64_t m_EpochSinceUs = 0;
    uint64_t m_EpochCandidateMilliHz = 0;
    uint64_t m_EpochCandidateSinceUs = 0;
    std::deque<std::pair<int, uint64_t>> m_EpochDemands;

    Pending m_Pending;
    Stats m_Stats;
};

}
