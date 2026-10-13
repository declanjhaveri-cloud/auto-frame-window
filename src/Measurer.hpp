#pragma once
// Pure measuring logic. No Geode / GD dependencies, so it can be unit tested.
//
// Flow:
//   1. Calibrate: replay the untouched macro with a few (frame source, offset)
//      combos until one reaches the end of the level. That tells us how the bot
//      that recorded the macro counted frames.
//   2. Measure: for every target input, shift it 1, 2, 3... frames earlier until
//      the player dies, then later. window = 1 + earlyFrames + lateFrames.

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace afw {

struct Event {
    int frame = 0;
    int button = 1;     // 1 jump, 2 left, 3 right
    bool p2 = false;
    bool down = true;
    double frac = 0.0;  // CBF: fire this far into the tick (0..1). Only set on the shifted input.
};

enum class FrameSource { Progress, LevelTime };

struct Candidate {
    FrameSource source = FrameSource::Progress;
    int offset = 0;     // input fires when (currentFrame + offset) >= event.frame
};

struct Settings {
    int maxShift = 8;            // frames tested in each direction
    int horizon = 240;           // frames that must be survived after the input
    bool measureReleases = false;
    bool autoSource = true;
    FrameSource forcedSource = FrameSource::Progress;
    bool preferLevelTime = false;  // xdBot macros count frames from level time
    std::optional<Candidate> preferred;  // known-good sync from a previous run, tried first
    int minFrame = 0;              // only measure inputs in [minFrame, maxFrame]
    int maxFrame = INT_MAX;
    int subdiv = 1;                // CBF beta: 2 = half frames, 4 = quarter frames, ...
    int calibrationTail = 5 * 240; // frames past last input that count as "made it"
};

struct Result {
    int index = 0;      // index into the sorted event list
    Event ev;
    double early = 0;   // frames it can be pressed early and still survive
    double late = 0;
    bool earlyCapped = false;   // hit maxShift or a neighbouring input, real window may be bigger
    bool lateCapped = false;
    double window() const { return 1.0 + early + late; }
};

struct Attempt {
    std::vector<Event> schedule;    // sorted by frame
    int stopFrame = INT_MAX;        // survive to this frame (or level end) = pass
    Candidate cand;
};

class Measurer {
public:
    enum class Phase { Idle, Calibrating, Measuring, Done, Failed };

    void init(std::vector<Event> events, Settings s) {
        m_settings = s;
        m_sub = std::max(1, s.subdiv);
        m_events = std::move(events);
        std::stable_sort(m_events.begin(), m_events.end(),
            [](Event const& a, Event const& b) { return a.frame < b.frame; });

        m_targets.clear();
        for (int i = 0; i < (int)m_events.size(); i++) {
            if (m_events[i].frame < s.minFrame || m_events[i].frame > s.maxFrame) continue;
            if (m_events[i].down || s.measureReleases) m_targets.push_back(i);
        }

        m_candidates.clear();
        std::vector<FrameSource> sources;
        if (s.autoSource && s.preferLevelTime) sources = { FrameSource::LevelTime, FrameSource::Progress };
        else if (s.autoSource) sources = { FrameSource::Progress, FrameSource::LevelTime };
        else sources = { s.forcedSource };
        if (s.preferred) m_candidates.push_back(*s.preferred);
        for (int off : { 0, -1, 1, -2, 2 }) {
            for (auto src : sources) {
                if (s.preferred && s.preferred->source == src && s.preferred->offset == off) continue;
                m_candidates.push_back({ src, off });
            }
        }

        m_results.clear();
        m_candIdx = 0;
        m_bestDeath = -1;
        m_error.clear();
        m_phase = m_events.empty() ? Phase::Failed : Phase::Calibrating;
        if (m_events.empty()) m_error = "The macro has no inputs.";
        m_targetPos = 0;
        m_dir = -1;
        m_shift = m_sub;
        m_step = m_sub;
        m_lastGood = 0;
        buildAttempt();
    }

    Phase phase() const { return m_phase; }
    bool finished() const { return m_phase == Phase::Done || m_phase == Phase::Failed; }
    std::string const& error() const { return m_error; }
    Attempt const& attempt() const { return m_attempt; }
    Candidate chosen() const { return m_chosen; }
    std::vector<Result> const& results() const { return m_results; }
    std::vector<Event> const& events() const { return m_events; }
    int targetCount() const { return (int)m_targets.size(); }
    int targetsDone() const { return m_targetPos; }
    int calibrationTry() const { return m_candIdx + 1; }
    int calibrationTotal() const { return (int)m_candidates.size(); }
    double currentShift() const { return m_dir * m_shift / double(m_sub); }

    // survived: no death before stopFrame (reaching the level end also counts).
    // frame: frame the attempt ended on (death frame if it died).
    void report(bool survived, int frame) {
        if (m_phase == Phase::Calibrating) {
            if (survived) {
                m_chosen = m_candidates[m_candIdx];
                m_phase = m_targets.empty() ? Phase::Done : Phase::Measuring;
                startTarget();
            }
            else {
                m_bestDeath = std::max(m_bestDeath, frame);
                if (++m_candIdx >= (int)m_candidates.size()) {
                    m_phase = Phase::Failed;
                    m_error = "The macro doesn't replay cleanly here (best try died at frame "
                        + std::to_string(m_bestDeath) + "). Check it's the right level, "
                        "no start position is active, and the macro was recorded at 240 TPS.";
                }
            }
        }
        else if (m_phase == Phase::Measuring) {
            Result& r = m_results.back();
            if (survived) {
                m_lastGood = m_shift;
                double v = m_shift / double(m_sub);
                if (m_dir < 0) r.early = v; else r.late = v;
                int next = m_shift + m_step;
                if (next > m_settings.maxShift * m_sub || !legalShift(r.index, m_dir * next)) {
                    if (m_dir < 0) r.earlyCapped = true; else r.lateCapped = true;
                    nextDirection();
                }
                else m_shift = next;
            }
            else if (m_step > 1 && m_lastGood + 1 < m_shift) {
                // Whole frames found the edge; refine between the last good and this fail.
                m_step = 1;
                m_shift = m_lastGood + 1;
            }
            else {
                nextDirection();
            }
        }
        buildAttempt();
    }

    // Stop early and keep whatever finished.
    std::vector<Result> completedResults() const {
        std::vector<Result> out = m_results;
        if (m_phase == Phase::Measuring && !out.empty()) out.pop_back();
        return out;
    }

private:
    void startTarget() {
        if (m_phase != Phase::Measuring) return;
        // Skip until a target can actually be tested in some direction.
        while (m_targetPos < (int)m_targets.size()) {
            int idx = m_targets[m_targetPos];
            Result r;
            r.index = idx;
            r.ev = m_events[idx];
            m_results.push_back(r);
            m_dir = -1;
            resetDir();
            if (advanceToTestable()) return;
            // neither direction testable; result already recorded, move on
            m_targetPos++;
        }
        m_phase = Phase::Done;
    }

    // Ensures (m_dir, m_shift) is a legal shift for the current target.
    // Returns false if both directions are exhausted.
    bool advanceToTestable() {
        Result& r = m_results.back();
        while (true) {
            if (legalShift(r.index, m_dir * m_shift)) return true;
            if (m_dir < 0) r.earlyCapped = true; else r.lateCapped = true;
            if (m_dir < 0) { m_dir = 1; resetDir(); continue; }
            return false;
        }
    }

    void nextDirection() {
        if (m_dir < 0) {
            m_dir = 1;
            resetDir();
            if (advanceToTestable()) return;
        }
        m_targetPos++;
        startTarget();
    }

    void resetDir() {
        m_shift = m_sub;
        m_step = m_sub;
        m_lastGood = 0;
    }

    // A shifted input can't cross the neighbouring input of the same button and
    // player (a press can't move past its own release), and can't go below frame 1.
    bool legalShift(int idx, int deltaUnits) const {
        Event const& e = m_events[idx];
        double nf = e.frame + deltaUnits / double(m_sub);
        if (nf < 1.0) return false;
        for (int j = idx - 1; j >= 0; j--) {
            auto const& o = m_events[j];
            if (o.button == e.button && o.p2 == e.p2) { if (nf <= double(o.frame)) return false; break; }
        }
        for (int j = idx + 1; j < (int)m_events.size(); j++) {
            auto const& o = m_events[j];
            if (o.button == e.button && o.p2 == e.p2) { if (nf >= double(o.frame)) return false; break; }
        }
        return true;
    }

    void buildAttempt() {
        if (m_phase == Phase::Calibrating) {
            m_attempt.schedule = m_events;
            m_attempt.stopFrame = m_events.back().frame + m_settings.calibrationTail;
            m_attempt.cand = m_candidates[m_candIdx];
        }
        else if (m_phase == Phase::Measuring) {
            int idx = m_results.back().index;
            double t = m_events[idx].frame + m_dir * m_shift / double(m_sub);
            double fl = std::floor(t + 1e-9);
            m_attempt.schedule = m_events;
            m_attempt.schedule[idx].frame = (int)fl;
            m_attempt.schedule[idx].frac = std::max(0.0, t - fl);
            std::stable_sort(m_attempt.schedule.begin(), m_attempt.schedule.end(),
                [](Event const& a, Event const& b) { return a.frame < b.frame; });
            int orig = m_events[idx].frame;
            m_attempt.stopFrame = std::max(orig, (int)std::ceil(t)) + m_settings.horizon;
            m_attempt.cand = m_chosen;
        }
    }

    Settings m_settings;
    std::vector<Event> m_events;
    std::vector<int> m_targets;
    std::vector<Candidate> m_candidates;
    std::vector<Result> m_results;
    Candidate m_chosen;
    Attempt m_attempt;
    Phase m_phase = Phase::Idle;
    std::string m_error;
    int m_candIdx = 0;
    int m_bestDeath = -1;
    int m_targetPos = 0;
    int m_dir = -1;
    int m_shift = 1;     // in 1/m_sub frame units
    int m_step = 1;
    int m_lastGood = 0;
    int m_sub = 1;
};

} // namespace afw
