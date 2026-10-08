#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Vrr {

// The adaptive playout delay, owned by presented-interval quality.
//
// Each pair of adjacent presented frames is scored by how far the client
// moved their spacing from the intended source spacing. Intervals within the
// tolerance are clean; the excess of others, relative to the intended
// interval, is a loss weighted by its duration. The share of loss over the
// preset's history window is compared with the preset's target.
//
// The delay grows only when all of these hold:
// - the long-window score is below target and this interval is too,
// - the interval error is fresh (above tolerance),
// - the delayed frame was late against its own deadline (lateness is
//   attributable to readiness, not to scheduling after it), and
// - the work was absorbable: decode/preparation service and decoder queueing
//   over the last second fit within the intended time. More buffering cannot
//   fix a pipeline that cannot keep up; dropping frames does.
// Growth is at most 250 us per 250 ms, bounded by the lateness that caused it.
//
// The delay releases at 250 us/s only after eight seconds of qualified clean
// evidence; short sequence breaks (a dropped frame, a phase change) keep the
// earned evidence, a capacity dip pauses it. Old score debt never authorizes
// growth by itself. This attack/release asymmetry reacts quickly to late
// frames and slowly to calm.
class IntervalBuffer {
public:
    struct Sample {
        uint64_t frame = 0;
        // Intended (source + deliberate retiming) time of the frame.
        uint64_t intended = 0;
        uint64_t submitted = 0;
        // The frame's own presentation deadline and readiness.
        uint64_t deadline = 0;
        uint64_t ready = 0;
        // Playout delay applied to the frame.
        uint64_t buffer = 0;
        bool valid = false;
        bool absorbable = false;
        uint64_t service = 0;
        uint64_t decoderQueue = 0;
    };

    struct Parameters {
        uint64_t minimumUs = 1000;
        uint64_t maximumUs = 1000;
        uint64_t targetPerMillion = 995000;
        uint64_t toleranceUs = 500;
        uint64_t scoreWindowUs = 120000000;
        uint64_t holdUs = 8000000;
        uint64_t releaseUsPerSecond = 250;
    };

    enum class Action : uint8_t {
        Learning,
        SequenceBreak,
        Grow,
        Capped,
        Pressure,
        Hold,
        Release,
        Minimum,
        NotAbsorbable,
        Cooldown,
        NoFreshMiss,
    };

    struct Stats {
        uint64_t evaluatedUs = 0;
        double weightedLossUs = 0;
        uint64_t failedIntervals = 0;
        uint64_t intervals = 0;
        Action action = Action::Learning;
        bool calibrated = false;
        double lossFraction() const
        {
            return evaluatedUs ? (std::min)(1.0, weightedLossUs / double(evaluatedUs)) : 0.0;
        }
        double qualityPercent() const { return 100.0 * (1.0 - lossFraction()); }
    };

    static const char* actionName(Action action)
    {
        switch (action) {
        case Action::Learning: return "learning";
        case Action::SequenceBreak: return "sequence break";
        case Action::Grow: return "growing for late frames";
        case Action::Capped: return "growth capped";
        case Action::Pressure: return "holding (late frames)";
        case Action::Hold: return "holding";
        case Action::Release: return "releasing";
        case Action::Minimum: return "minimum";
        case Action::NotAbsorbable: return "work not absorbable";
        case Action::Cooldown: return "growth cooldown";
        case Action::NoFreshMiss: return "no fresh late frame";
        }
        return "unknown";
    }

    // The requested delay, or `applied` before the first observation.
    uint64_t demand(uint64_t applied) const { return m_Initialized ? m_Target : applied; }
    const Stats& stats() const { return m_Stats; }

    void reset() { *this = IntervalBuffer{}; }

    void observe(const Sample& s, const Parameters& p)
    {
        const uint64_t minimum = (std::min)(p.minimumUs, p.maximumUs);
        if (!m_Initialized) {
            m_Target = s.buffer;
            m_Initialized = true;
        }
        m_Target = (std::clamp)(m_Target, minimum, p.maximumUs);

        // Earned clean time does not survive an unobserved gap or a long
        // stretch of ineligible frames.
        if (s.submitted) {
            const bool gap = m_LastObservedUs &&
                (s.submitted <= m_LastObservedUs || s.submitted - m_LastObservedUs >= 1000000);
            const bool ineligible = m_LastCleanUs && s.submitted > m_LastCleanUs &&
                s.submitted - m_LastCleanUs >= 2000000;
            if (gap || ineligible) {
                m_CleanUs = 0;
                m_LastCleanUs = 0;
            }
            m_LastObservedUs = s.submitted;
        }

        const Sample previous = m_Previous;
        const bool adjacent = m_HavePrevious && s.valid && s.frame == previous.frame + 1 &&
            s.submitted > previous.submitted && s.intended > previous.intended &&
            s.submitted - previous.submitted < 1000000 && s.intended - previous.intended < 1000000;
        m_Previous = s;
        m_HavePrevious = s.valid && s.submitted && s.intended;
        if (!adjacent) {
            breakSequence();
            updateScore(s.submitted, p.scoreWindowUs);
            return;
        }

        const uint64_t actual = s.submitted - previous.submitted;
        const uint64_t intended = s.intended - previous.intended;
        const uint64_t error = (std::max)(actual, intended) - (std::min)(actual, intended);

        // One-second service window, 10 ms buckets.
        const uint64_t tick = s.submitted / 10000;
        Bucket& bucket = m_Window[tick % m_Window.size()];
        if (bucket.tick != tick) {
            bucket = Bucket{tick};
        }
        bucket.service += s.service;
        bucket.decoderQueue += s.decoderQueue;
        bucket.intended += intended;
        ++bucket.samples;
        if (!m_First) {
            m_First = s.submitted;
            // Only a fresh controller uses the faster initial qualification.
            m_WarmupUs = m_Calibrated ? 1000000 : 500000;
            m_WarmupSamples = m_Calibrated ? 2 : 32;
        }
        ++m_SequenceSamples;
        uint64_t samples = 0, service = 0, decoderQueue = 0, intendedTime = 0;
        for (const Bucket& b : m_Window) {
            if (tick >= b.tick && tick - b.tick < m_Window.size()) {
                samples += b.samples;
                service += b.service;
                decoderQueue += b.decoderQueue;
                intendedTime += b.intended;
            }
        }
        const bool qualified = samples >= 2 && m_SequenceSamples >= m_WarmupSamples &&
                               s.submitted - m_First >= m_WarmupUs;
        m_Stats.calibrated = qualified;
        if (!qualified) {
            m_Stats.action = Action::Learning;
            updateScore(s.submitted, p.scoreWindowUs);
            return;
        }
        m_Calibrated = true;

        // Score excess per interval, weighted by evaluated time, not frame count.
        const double excessUs = (std::max)(0.0, double(error) - double(p.toleranceUs));
        const double loss = (std::min)(1.0, excessUs / double(intended));
        ScoreBucket& score = m_Score[(s.submitted / kScoreBucketUs) % m_Score.size()];
        if (score.tick != s.submitted / kScoreBucketUs) {
            score = ScoreBucket{s.submitted / kScoreBucketUs};
        }
        score.evaluated += actual;
        score.weightedLoss += double(actual) * loss;
        score.intervals += 1;
        score.failed += error > p.toleranceUs ? 1 : 0;
        updateScore(s.submitted, p.scoreWindowUs);

        const double allowedLoss = double(1000000 - std::min<uint64_t>(p.targetPerMillion, 1000000)) / 1e6;
        const bool belowTarget = m_Stats.lossFraction() > allowedLoss;
        const bool currentPressure = loss > allowedLoss;
        const Sample& delayed = actual >= intended ? s : previous;
        const uint64_t lateness = delayed.ready > delayed.deadline ? delayed.ready - delayed.deadline : 0;
        const bool freshError = error > p.toleranceUs;
        const bool windowAbsorbable = service <= intendedTime && decoderQueue <= intendedTime;
        const bool delayedAbsorbable = delayed.absorbable && windowAbsorbable;
        const bool pressureHolds = belowTarget && currentPressure && freshError && delayedAbsorbable && lateness;

        if (pressureHolds) {
            m_LastPressure = s.submitted;
            m_ReleaseFraction = 0;
            m_CleanUs = 0;
            m_LastCleanUs = 0;
        }
        else if (belowTarget && (!s.absorbable || !windowAbsorbable)) {
            // A capacity dip pauses earned recovery.
            m_CleanUs = 0;
            m_LastCleanUs = 0;
        }
        else if (s.absorbable && windowAbsorbable) {
            m_CleanUs = (std::min)(p.holdUs, m_CleanUs + std::min<uint64_t>(actual, 100000));
            m_LastCleanUs = s.submitted;
        }

        m_Stats.action = pressureHolds ? Action::Pressure : Action::Hold;
        const bool grow = currentPressure && belowTarget;
        if (grow && freshError && delayedAbsorbable && lateness &&
            (!m_LastAttack || s.submitted - m_LastAttack >= 250000)) {
            const uint64_t excess = uint64_t(std::ceil((std::min)(250.0,
                (std::max)(0.0, excessUs - allowedLoss * double(intended)))));
            const uint64_t increase = (std::min)({uint64_t(250), excess, lateness, error - p.toleranceUs});
            const uint64_t base = (std::min)(delayed.buffer, p.maximumUs);
            const uint64_t room = p.maximumUs - base;
            m_Target = (std::max)(m_Target, base + (std::min)(increase, room));
            m_Stats.action = increase > room ? Action::Capped : Action::Grow;
            m_LastAttack = s.submitted;
            m_ReleaseFraction = 0;
        }
        else if (!pressureHolds && s.absorbable && windowAbsorbable && m_CleanUs >= p.holdUs &&
                 (!m_LastPressure || s.submitted - m_LastPressure >= p.holdUs)) {
            m_ReleaseFraction += std::min<uint64_t>(actual, 100000) * p.releaseUsPerSecond;
            const uint64_t release = m_ReleaseFraction / 1000000;
            m_ReleaseFraction %= 1000000;
            m_Target -= (std::min)(m_Target - minimum, release);
            m_Stats.action = m_Target == minimum ? Action::Minimum : Action::Release;
        }
        else if (grow) {
            m_Stats.action = !delayedAbsorbable ? Action::NotAbsorbable :
                             (!freshError || !lateness) ? Action::NoFreshMiss : Action::Cooldown;
        }
    }

    // Replace the standing target (a known source rate came back) and treat
    // the change as fresh pressure, so it is not released at once.
    void restoreTarget(uint64_t target, uint64_t atUs)
    {
        if (!m_Initialized) {
            return;
        }
        m_Target = target;
        m_LastPressure = atUs;
        m_ReleaseFraction = 0;
    }

private:
    static constexpr uint64_t kScoreBucketUs = 100000;
    static constexpr size_t kScoreBuckets = 3000; // five minutes

    struct Bucket {
        uint64_t tick = 0, samples = 0, service = 0, decoderQueue = 0, intended = 0;
    };
    struct ScoreBucket {
        uint64_t tick = 0, evaluated = 0, intervals = 0, failed = 0;
        double weightedLoss = 0;
    };

    void breakSequence()
    {
        m_Window = {};
        m_First = 0;
        m_ReleaseFraction = 0;
        m_SequenceSamples = 0;
        m_Stats.action = Action::SequenceBreak;
        m_Stats.calibrated = false;
    }

    void updateScore(uint64_t at, uint64_t windowUs)
    {
        m_Stats.evaluatedUs = 0;
        m_Stats.weightedLossUs = 0;
        m_Stats.intervals = 0;
        m_Stats.failedIntervals = 0;
        const uint64_t buckets = std::clamp<uint64_t>((windowUs + kScoreBucketUs - 1) / kScoreBucketUs,
                                                        1, kScoreBuckets);
        const uint64_t tick = at / kScoreBucketUs;
        for (const ScoreBucket& b : m_Score) {
            if (b.evaluated && tick >= b.tick && tick - b.tick < buckets) {
                m_Stats.evaluatedUs += b.evaluated;
                m_Stats.weightedLossUs += b.weightedLoss;
                m_Stats.intervals += b.intervals;
                m_Stats.failedIntervals += b.failed;
            }
        }
    }

    std::array<Bucket, 100> m_Window{};
    // Five minutes of 100 ms buckets, allocated once per controller.
    std::vector<ScoreBucket> m_Score = std::vector<ScoreBucket>(kScoreBuckets);
    Sample m_Previous;
    Stats m_Stats;
    uint64_t m_Target = 0, m_First = 0, m_LastAttack = 0, m_LastPressure = 0, m_ReleaseFraction = 0;
    uint64_t m_CleanUs = 0, m_LastCleanUs = 0, m_LastObservedUs = 0;
    uint64_t m_WarmupUs = 500000, m_SequenceSamples = 0, m_WarmupSamples = 32;
    bool m_HavePrevious = false, m_Initialized = false, m_Calibrated = false;
};

}
