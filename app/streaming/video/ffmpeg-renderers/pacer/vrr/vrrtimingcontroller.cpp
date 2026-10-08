#include "vrrtimingcontroller.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Vrr {

namespace {

constexpr uint64_t kDelayMinimumUs = 1000;
constexpr uint64_t kDelayStartUs = 6000;
constexpr uint64_t kDelayStartPeriodPerMille = 950;
// The interval buffer's requested delay is applied gradually.
constexpr uint64_t kDelayStepUs = 125;
constexpr uint64_t kRenderLeadFloorUs = 3000;
constexpr size_t kPreparationSamples = 96;
// Present synchronized when the panel may be repeating the previous frame
// below its VRR range: an immediate present could land mid-repeat.
constexpr uint64_t kVrrFloorGapUs = 20000;
constexpr uint64_t kFutureReseedFrames = 3;
constexpr uint64_t kLateTargetUs = 1000;
// Rate epochs: a change of 1.5x or more, confirmed for a second, after the
// previous rate held for ten seconds.
constexpr uint64_t kEpochRatioPerMille = 1500;
constexpr uint64_t kEpochConfirmUs = 1000000;
constexpr uint64_t kEpochSustainUs = 10000000;

uint64_t periodForRate(int rateHz, uint64_t fallback)
{
    return rateHz > 0 ? (kMicrosecondsPerSecond + uint64_t(rateHz) / 2) / uint64_t(rateHz) : fallback;
}

uint64_t addSigned(uint64_t value, int64_t delta)
{
    if (delta >= 0) {
        return value + uint64_t(delta);
    }
    return value > uint64_t(-delta) ? value - uint64_t(-delta) : 0;
}

uint64_t percentile(const std::deque<uint64_t>& values, unsigned percent)
{
    if (values.empty()) {
        return 0;
    }
    std::vector<uint64_t> sorted(values.begin(), values.end());
    const size_t index = (std::min)(sorted.size() - 1, (sorted.size() * percent + 99) / 100 - 1);
    std::nth_element(sorted.begin(), sorted.begin() + index, sorted.end());
    return sorted[index];
}

// A soft cadence floor while recovering from late work: start gently, use
// more of the display headroom as queued work ages toward the stale limit.
// Never a new source clock or extra buffer demand.
uint64_t catchUpFloorUs(uint64_t lastSubmission, uint64_t sourcePeriod, uint64_t displayFloorPeriod,
                        uint64_t queueAge)
{
    constexpr uint64_t kGentlePerMille = 20;
    if (!lastSubmission || !sourcePeriod || displayFloorPeriod >= sourcePeriod ||
        sourcePeriod > kMicrosecondsPerSecond) {
        return 0;
    }
    const uint64_t headroom = sourcePeriod - displayFloorPeriod;
    const uint64_t gentle = (std::min)(headroom, sourcePeriod * kGentlePerMille / 1000);
    const uint64_t pressure = queueAge > sourcePeriod ? (std::min)(sourcePeriod, queueAge - sourcePeriod) : 0;
    const uint64_t recovery = gentle + (headroom - gentle) * pressure / sourcePeriod;
    return lastSubmission + (sourcePeriod - recovery);
}

}

TimingController::TimingController(const SessionConfig& config) :
    m_Config(config),
    m_DisplayPeriodUs(periodForRate(config.displayRefreshHz, 16667)),
    // A guard of 1/96 of the display period, between 100 and 250 us.
    m_GuardUs(std::clamp<uint64_t>(periodForRate(config.displayRefreshHz, 16667) / 96, 100, 250)),
    m_Clock(config.streamRateHz, periodForRate(config.displayRefreshHz, 16667))
{
    m_Clock.setGuardUs(m_GuardUs);
    reset();
}

void TimingController::reset()
{
    m_Clock.reset(true);
    m_Offset.reset();
    m_Buffer.reset();
    m_Smoother.reset();
    m_DelayInitialized = false;
    m_DelayUs = 0;
    m_FutureProjections = 0;
    m_HaveLastSubmission = false;
    m_LastSubmissionUs = 0;
    m_SpacingAnchorUs = 0;
    m_Latched = false;
    m_CatchUpActive = false;
    m_Preparations.clear();
    m_FirstPreparationSeen = false;
    m_RenderLeadUs = kRenderLeadFloorUs;
    m_TypicalPreparationUs = 0;
    m_EpochRateMilliHz = 0;
    m_EpochSinceUs = 0;
    m_EpochCandidateMilliHz = 0;
    m_EpochCandidateSinceUs = 0;
    m_EpochDemands.clear();
    m_Pending = Pending{};
    m_Stats = Stats{};
}

void TimingController::rebase()
{
    // A new timeline keeps learned rate, delay and preparation budgets but
    // drops the clock mapping and any cadence it could no longer continue.
    m_Offset.reset();
    m_Smoother.resetCadence();
    m_CatchUpActive = false;
    m_FutureProjections = 0;
    ++m_Stats.rebases;
}

uint64_t TimingController::queueLimitUs() const
{
    // One frame active, four waiting: the playout delay plus preparation and
    // the smoothing allowance must fit in the frames the worker may hold.
    const uint64_t period = (std::min)(m_Clock.sourcePeriodUs(), m_Clock.streamPeriodUs());
    const uint64_t capacity = period * uint64_t(kQueuedFrames);
    const uint64_t occupied = m_RenderLeadUs + (m_Config.reduceJudder ? uint64_t(CadenceSmoother::kMaxLagUs) : 0);
    return capacity > occupied ? capacity - occupied : 0;
}

uint64_t TimingController::delayMaximumUs() const
{
    const uint64_t preset = (std::max)(kDelayMinimumUs,
        m_Clock.sourcePeriodUs() * uint64_t(m_Config.timing.bufferPerMille) / 1000);
    return (std::min)(preset, queueLimitUs());
}

uint64_t TimingController::delayMinimumUs() const
{
    return (std::min)(kDelayMinimumUs, delayMaximumUs());
}

uint64_t TimingController::typicalPreparationUs() const
{
    return m_TypicalPreparationUs;
}

void TimingController::updateDelay()
{
    const uint64_t minimum = delayMinimumUs();
    const uint64_t maximum = delayMaximumUs();
    if (!m_DelayInitialized) {
        const uint64_t period = m_Clock.sourcePeriodUs();
        uint64_t start = (std::max)(kDelayStartUs, period * kDelayStartPeriodPerMille / 1000);
        start = (std::min)(start, (std::max)(m_DisplayPeriodUs, m_RenderLeadUs));
        m_DelayUs = (std::clamp)(start, minimum, maximum);
        m_DelayInitialized = true;
        return;
    }
    const uint64_t requested = (std::clamp)(m_Buffer.demand(m_DelayUs), minimum, maximum);
    if (requested > m_DelayUs) {
        m_DelayUs = (std::min)(requested, m_DelayUs + kDelayStepUs);
    }
    else if (requested < m_DelayUs) {
        m_DelayUs = (std::max)(requested, m_DelayUs > kDelayStepUs ? m_DelayUs - kDelayStepUs : 0);
    }
    m_DelayUs = (std::clamp)(m_DelayUs, minimum, maximum);
}

void TimingController::noteRateEpoch(uint64_t atUs)
{
    const uint64_t period = m_Clock.sourcePeriodUs();
    if (!period) {
        return;
    }
    const uint64_t rate = kMicrosecondsPerSecond * 1000 / period;
    if (!m_EpochRateMilliHz) {
        m_EpochRateMilliHz = rate;
        m_EpochSinceUs = atUs;
        return;
    }
    const auto departs = [](uint64_t a, uint64_t b) {
        return a * 1000 >= b * kEpochRatioPerMille || b * 1000 >= a * kEpochRatioPerMille;
    };
    if (!departs(rate, m_EpochRateMilliHz)) {
        m_EpochCandidateMilliHz = 0;
        return;
    }
    if (!m_EpochCandidateMilliHz || departs(rate, m_EpochCandidateMilliHz)) {
        m_EpochCandidateMilliHz = rate;
        m_EpochCandidateSinceUs = atUs;
    }
    if (atUs < m_EpochCandidateSinceUs || atUs - m_EpochCandidateSinceUs < kEpochConfirmUs) {
        return;
    }
    m_EpochCandidateMilliHz = 0;
    // Quarter-octave bands: nearby rates of one source share a demand.
    const auto band = [](uint64_t milliHz) {
        return int(std::lround(std::log2(double(milliHz) / 1000.0) * 4.0));
    };
    const uint64_t current = m_Buffer.demand(m_DelayUs);
    if (atUs >= m_EpochSinceUs && atUs - m_EpochSinceUs >= kEpochSustainUs) {
        const int outgoing = band(m_EpochRateMilliHz);
        auto it = std::find_if(m_EpochDemands.begin(), m_EpochDemands.end(),
                               [outgoing](const auto& entry) { return entry.first == outgoing; });
        if (it != m_EpochDemands.end()) {
            it->second = current;
        }
        else {
            m_EpochDemands.emplace_back(outgoing, current);
            if (m_EpochDemands.size() > 16) {
                m_EpochDemands.pop_front();
            }
        }
    }
    const int incoming = band(rate);
    const std::pair<int, uint64_t>* nearest = nullptr;
    for (const auto& entry : m_EpochDemands) {
        if (std::abs(entry.first - incoming) <= 1 &&
            (!nearest || std::abs(entry.first - incoming) < std::abs(nearest->first - incoming))) {
            nearest = &entry;
        }
    }
    // Delay bought at another rate is no evidence for this one: restore only downward.
    if (nearest && nearest->second < current) {
        m_Buffer.restoreTarget(nearest->second, atUs);
    }
    m_EpochRateMilliHz = rate;
    m_EpochSinceUs = atUs;
}

Decision TimingController::schedule(const FrameTiming& frame, uint64_t nowUs)
{
    m_Pending = Pending{};
    ++m_Stats.scheduled;

    Cadence cadence;
    bool rebased = false;
    if (!m_Clock.started()) {
        m_Clock.start(frame);
        rebased = true;
    }
    else {
        cadence = m_Clock.observe(frame);
        if (cadence.rebase) {
            rebase();
            m_Clock.start(frame);
            rebased = true;
            cadence = Cadence{};
        }
    }
    if (!rebased && cadence.sourceRateChanged) {
        ++m_Stats.rateChanges;
        m_Smoother.resetCadence();
    }
    if (!rebased && cadence.phaseDiscontinuity) {
        ++m_Stats.phaseResets;
    }

    const uint64_t readyUs = frame.readyUs ? frame.readyUs : nowUs;
    const bool timestampPlayout = frame.timestampValid && (rebased || cadence.usedRtp);
    const uint64_t rtpUs = m_Clock.rtpUs();
    uint64_t sourceTimeUs;
    if (timestampPlayout) {
        const int64_t offset = int64_t(readyUs) - int64_t(rtpUs);
        // Jitter only makes frames later than the earliest arrival; a whole
        // second of frames later than slot + delay + one period is a path change.
        const uint64_t reanchorUs = (m_DelayInitialized ? m_DelayUs : kDelayStartUs) + m_Clock.sourcePeriodUs();
        const int64_t applied = m_Offset.observe(rtpUs, offset, rebased || cadence.eligible,
                                                 cadence.phaseDiscontinuity, reanchorUs);
        sourceTimeUs = addSigned(rtpUs, applied);
    }
    else {
        // Without timestamps the arrival is the only source time.
        m_Offset.reset();
        sourceTimeUs = readyUs;
    }
    int64_t readyOffsetUs = int64_t(readyUs) - int64_t(sourceTimeUs);

    if (!rebased && cadence.eligible) {
        noteRateEpoch(readyUs);
    }
    // The slot uses the delay in force before this frame's evidence.
    updateDelay();
    const uint64_t delayUs = m_DelayUs;

    int64_t smoothingUs = 0;
    int64_t requestedRetimingUs = 0;
    uint64_t readinessPushUs = 0;
    if (m_Config.reduceJudder && timestampPlayout) {
        const CadenceSmoother::Result result = m_Smoother.adjust(
            cadence, rebased, m_Clock.sourcePeriodUs(), sourceTimeUs + delayUs, delayUs, readyOffsetUs);
        smoothingUs = result.smoothingUs;
        requestedRetimingUs = result.requestedUs;
        readinessPushUs = result.readinessPushUs;
    }
    else {
        m_Smoother.resetCadence();
    }

    const uint64_t preparationUs = typicalPreparationUs();
    uint64_t targetUs = addSigned(sourceTimeUs, smoothingUs) + delayUs + preparationUs;
    uint64_t intendedUs = targetUs;
    targetUs = (std::max)(targetUs, nowUs + preparationUs);

    // A run of frames mapping far into the future means the host clock
    // jumped: reseed the mapping on this frame rather than waiting it out.
    // One early outlier simply waits for its slot.
    const uint64_t futureLimitUs = nowUs + uint64_t(std::max<int64_t>(smoothingUs, 0)) +
        (std::max)(m_Clock.streamPeriodUs(), m_Clock.sourcePeriodUs()) + delayUs + m_RenderLeadUs;
    bool reseeded = false;
    if (targetUs > futureLimitUs && timestampPlayout) {
        if (++m_FutureProjections >= kFutureReseedFrames) {
            m_FutureProjections = 0;
            reseeded = true;
            m_Offset.reset();
            m_Offset.observe(rtpUs, int64_t(readyUs) - int64_t(rtpUs), true, false);
            m_Smoother.resetCadence();
            sourceTimeUs = readyUs;
            readyOffsetUs = 0;
            smoothingUs = 0;
            requestedRetimingUs = 0;
            readinessPushUs = 0;
            targetUs = (std::max)(readyUs, nowUs) + delayUs + m_RenderLeadUs;
            intendedUs = targetUs;
            cadence.phaseDiscontinuity = true;
            cadence.eligible = false;
            ++m_Stats.phaseResets;
        }
    }
    else {
        m_FutureProjections = 0;
    }

    if (m_Config.reduceJudder && timestampPlayout && !reseeded) {
        const int64_t rawLateness = readyOffsetUs - int64_t(delayUs);
        m_Smoother.observeReserve(m_Smoother.engaged(), std::min<int64_t>(rawLateness, 0) - requestedRetimingUs,
                                  nowUs);
        m_Smoother.commitBasis(addSigned(sourceTimeUs + delayUs, smoothingUs - int64_t(readinessPushUs)));
    }

    // Per-frame protection: a slot within one display period (plus guard) of
    // the previous flip, or one after a gap below the VRR range, is latched
    // when the presenter synchronizes natively.
    const uint64_t anchorUs = (std::max)(m_SpacingAnchorUs, m_LastSubmissionUs);
    m_Latched = false;
    if (m_Config.protection == PresentProtection::Native && m_HaveLastSubmission) {
        m_Latched = targetUs < anchorUs + m_DisplayPeriodUs + m_GuardUs ||
                    targetUs >= anchorUs + kVrrFloorGapUs;
    }
    const uint64_t beforeFloorUs = targetUs;
    targetUs = (std::max)(targetUs, earliestSubmissionUs());

    // Late recovery: spread a catch-up over the display headroom.
    const uint64_t periodUs = m_Clock.sourcePeriodUs();
    const uint64_t queueAgeUs = nowUs > readyUs ? nowUs - readyUs : 0;
    const bool recoveryEligible = timestampPlayout && !rebased && !reseeded && cadence.eligible &&
        !cadence.phaseDiscontinuity && !cadence.sourceRateChanged && m_HaveLastSubmission &&
        periodUs <= kMicrosecondsPerSecond && cadence.intervalUs >= periodUs * 9 / 10 &&
        cadence.intervalUs <= periodUs * 11 / 10;
    if (recoveryEligible && (m_CatchUpActive || queueAgeUs > periodUs)) {
        const uint64_t pressureAgeUs = queueAgeUs - (std::min)(queueAgeUs, delayUs);
        const uint64_t floorUs = catchUpFloorUs(m_LastSubmissionUs, periodUs,
                                                m_DisplayPeriodUs + m_GuardUs, pressureAgeUs);
        const uint64_t horizonUs = (std::max)(periodUs * 2, delayUs + periodUs);
        const uint64_t hardDeadlineUs = readyUs + horizonUs;
        m_CatchUpActive = floorUs != 0 && (floorUs > targetUs || queueAgeUs > periodUs) &&
                          queueAgeUs < horizonUs && targetUs < hardDeadlineUs;
        if (m_CatchUpActive) {
            const uint64_t extraUs = std::min<uint64_t>(4000, periodUs / 2);
            targetUs = (std::max)(targetUs, (std::min)({floorUs, hardDeadlineUs, targetUs + extraUs}));
            ++m_Stats.catchUps;
        }
    }
    else {
        m_CatchUpActive = false;
    }

    Decision decision;
    decision.targetUs = targetUs;
    decision.intendedTargetUs = intendedUs;
    // Preparation may use the playout interval; the target does not move.
    const uint64_t leadUs = (std::min)(periodUs, m_RenderLeadUs) + delayUs;
    decision.renderStartUs = targetUs > leadUs ? targetUs - leadUs : 0;
    decision.sourceTimeUs = sourceTimeUs;
    decision.sourcePeriodUs = periodUs;
    decision.sourceIntervalUs = cadence.intervalUs;
    decision.playoutDelayUs = delayUs;
    decision.smoothingUs = smoothingUs;
    decision.readyOffsetUs = readyOffsetUs;
    decision.floorPushUs = targetUs > beforeFloorUs ? targetUs - beforeFloorUs : 0;
    decision.latched = m_Latched;
    decision.timestampPlayout = timestampPlayout;
    decision.rebased = rebased;
    decision.phaseDiscontinuity = !rebased && cadence.phaseDiscontinuity;
    decision.sourceRateChanged = !rebased && cadence.sourceRateChanged;
    decision.catchUp = m_CatchUpActive;

    m_Pending.valid = true;
    m_Pending.frameNumber = frame.frameNumber;
    m_Pending.intendedTargetUs = intendedUs;
    m_Pending.intervalIntendedUs = addSigned(sourceTimeUs, smoothingUs);
    // Advancing a frame for smoothing does not move its readiness deadline:
    // only lateness against the raw slot may grow the buffer.
    m_Pending.deadlineUs = intendedUs + (smoothingUs < 0 ? uint64_t(-smoothingUs) : 0);
    m_Pending.appliedDelayUs = delayUs;
    m_Pending.intervalValid = cadence.usedRtp && !rebased && !reseeded && !cadence.phaseDiscontinuity;
    // Only steady source intervals teach the buffer: not stalls, bursts,
    // transitions, or frames that lost packets (loss costs detail, not timing).
    m_Pending.absorbable = timestampPlayout && !rebased && cadence.usedRtp && cadence.frameDelta == 1 &&
        !cadence.phaseDiscontinuity && frame.lostPackets == 0 &&
        cadence.intervalUs <= std::max<uint64_t>(25000, periodUs * 3 / 2) &&
        cadence.intervalUs >= periodUs / 2;
    m_Pending.latched = m_Latched;
    if (decision.floorPushUs) {
        ++m_Stats.floorDelayed;
    }
    return decision;
}

uint64_t TimingController::earliestSubmissionUs() const
{
    if (!m_HaveLastSubmission) {
        return 0;
    }
    if (m_Latched && m_Config.protection == PresentProtection::Native) {
        // Synchronized presentation orders and spaces these flips itself; a
        // software floor could not sustain a source at the refresh rate.
        return 0;
    }
    return (std::max)(m_SpacingAnchorUs, m_LastSubmissionUs) + m_DisplayPeriodUs + m_GuardUs;
}

void TimingController::learnPreparation(uint64_t durationUs)
{
    // The first preparation of an epoch includes one-time renderer setup.
    if (!m_FirstPreparationSeen) {
        m_FirstPreparationSeen = true;
        return;
    }
    m_Preparations.push_back(durationUs);
    while (m_Preparations.size() > kPreparationSamples) {
        m_Preparations.pop_front();
    }
    const uint64_t periodUs = std::max<uint64_t>(1, m_Clock.sourcePeriodUs());
    m_TypicalPreparationUs = (std::min)(percentile(m_Preparations, 50), periodUs);
    m_RenderLeadUs = (std::clamp)(percentile(m_Preparations, 99), kRenderLeadFloorUs,
                                  (std::max)(kRenderLeadFloorUs, periodUs));
}

void TimingController::noteSubmission(const Submission& submission)
{
    if (!m_Pending.valid) {
        return;
    }
    const bool presented = submission.presented && !submission.cancelled;

    IntervalBuffer::Parameters parameters;
    parameters.minimumUs = delayMinimumUs();
    parameters.maximumUs = delayMaximumUs();
    parameters.targetPerMillion = uint64_t(m_Config.timing.targetHundredths) * 100;
    parameters.toleranceUs = uint64_t(m_Config.timing.toleranceUs);
    parameters.scoreWindowUs = uint64_t(m_Config.timing.historySeconds) * kMicrosecondsPerSecond;
    IntervalBuffer::Sample sample;
    sample.frame = m_Pending.frameNumber >= 0 ? uint64_t(m_Pending.frameNumber) : 0;
    sample.intended = m_Pending.intervalIntendedUs;
    sample.submitted = submission.submitUs;
    sample.deadline = m_Pending.deadlineUs;
    sample.ready = submission.readyUs;
    sample.buffer = m_Pending.appliedDelayUs;
    sample.valid = presented && m_Pending.intervalValid && submission.preparationUs != 0;
    sample.absorbable = m_Pending.absorbable;
    sample.service = submission.preparationUs;
    m_Buffer.observe(sample, parameters);

    if (submission.presented) {
        // A latched present flips no earlier than one display period after
        // the previous flip, not at its call.
        m_SpacingAnchorUs = m_HaveLastSubmission && m_Pending.latched ?
            (std::max)(submission.submitUs, (std::max)(m_SpacingAnchorUs, m_LastSubmissionUs) + m_DisplayPeriodUs) :
            submission.submitUs;
        m_HaveLastSubmission = true;
        m_LastSubmissionUs = submission.submitUs;
    }
    // A present that missed its target arms gradual recovery; timer noise does not.
    m_CatchUpActive = presented && m_Pending.intervalValid &&
                      (m_CatchUpActive || submission.submitUs > m_Pending.intendedTargetUs + 500);
    if (presented) {
        ++m_Stats.presented;
        m_Stats.latched += m_Pending.latched ? 1 : 0;
        if (submission.submitUs > m_Pending.intendedTargetUs + kLateTargetUs) {
            ++m_Stats.lateTargets;
        }
        if (submission.preparationUs) {
            learnPreparation(submission.preparationUs);
        }
    }
    m_Pending = Pending{};
}

uint64_t TimingController::staleHorizonUs(uint64_t successorIntervalUs) const
{
    const uint64_t periodUs = (std::max)(m_Clock.sourcePeriodUs(), successorIntervalUs);
    const uint64_t preparationUs = (std::min)(m_Clock.sourcePeriodUs(), m_RenderLeadUs);
    return (std::max)(periodUs * 2, m_DelayUs + preparationUs + periodUs);
}

TimingController::Stats TimingController::stats() const
{
    Stats stats = m_Stats;
    const IntervalBuffer::Stats& buffer = m_Buffer.stats();
    stats.sourcePeriodUs = m_Clock.sourcePeriodUs();
    stats.playoutDelayUs = m_DelayUs;
    stats.requestedDelayUs = m_Buffer.demand(m_DelayUs);
    stats.maximumDelayUs = delayMaximumUs();
    stats.renderLeadUs = m_RenderLeadUs;
    stats.smoothingReserveUs = m_Smoother.reserveUs();
    stats.intervalQualityPercent = buffer.qualityPercent();
    stats.intervals = buffer.intervals;
    stats.failedIntervals = buffer.failedIntervals;
    stats.bufferAction = buffer.action;
    stats.calibrated = buffer.calibrated;
    return stats;
}

}
