#pragma once
// L* (NaNDL precision) model, ported from Frame Window Counter by C0nscious
// (https://github.com/hyper-5/frame-window-counter, MIT license).
//
// Each click i at time t_i with window w_i (seconds) is hit with probability
// p_i = 1 - erfc(W_i * L), where W_i = w_i * 0.5 / sqrt(2) times optional decay
// multipliers. L* is the precision L at which the expected time to beat the level
// (with retries) equals the target time (24h by default). Higher L* = harder.
//
//   Nerve   (K_T): exp(-kT * t)        clicks later in the level count as harder
//   Fatigue (K_U): exp(-kU * n)        the nth click counts as harder
//   CPS     (K_C): (4 / max(1, 2/dt))^kC   fast clicking counts as harder

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

namespace afw::lstar {

enum Metric { Base = 0, Nerve, Fatigue, Cps, NF, NC, FC, All, MetricCount };

inline char const* metricName(int m) {
    static char const* names[] = { "Base", "Nerve", "Fatigue", "CPS", "N+F", "N+C", "F+C", "All" };
    return names[m];
}

struct Params {
    double fps = 240.0;
    double respawn = 0.0;          // seconds added to each click time
    double target = 86400.0;       // seconds (24h)
    double kT = 0.0016520833717346;
    double kU = 0.0002727763242154;
    double kC = 0.2784421686721826;
};

struct Click {
    double frame;
    double window;  // frames
};

// series[m][i] = L* for the level up to and including click i.
struct Output {
    std::array<std::vector<double>, MetricCount> series;
    double final(int m) const { return series[m].empty() ? 0.0 : series[m].back(); }
};

constexpr double kMinLeft = 0.001;
constexpr double kMaxRight = 1e15;

inline double erfcFast(double x) {
    // std::erfc is fast enough here and exact; the LUT in the original was an optimization.
    if (std::isnan(x)) return 1.0;
    if (x >= 6.0) return 0.0;
    if (x <= 0.0) return 1.0;
    return std::erfc(x);
}

// cancel() returning true aborts early (returns partial output).
inline Output compute(std::vector<Click> clicks, Params const& p,
    std::function<bool()> const& cancel = nullptr,
    std::atomic<float>* progress = nullptr) {
    Output out;
    std::stable_sort(clicks.begin(), clicks.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
    size_t N = clicks.size();
    if (N == 0) return out;

    std::vector<double> T(N);
    std::array<std::vector<double>, MetricCount> W;
    for (auto& w : W) w.assign(N, 0.0);

    double const mult = 0.5 * 0.7071067811865475;
    double prevTime = 0.0;
    double fps = p.fps > 0 ? p.fps : 240.0;
    auto clean = [](double v) { return (std::isnan(v) || std::isinf(v) || v < 0.0) ? 0.0 : v; };

    for (size_t i = 0; i < N; i++) {
        double t = p.respawn + clicks[i].frame / fps;
        T[i] = t;
        double w = (clicks[i].window <= 0 ? 1.0 : clicks[i].window) / fps;
        double n = std::exp(-p.kT * t);
        double f = std::exp(-p.kU * double(i + 1));
        if (std::isnan(f) || std::isinf(f)) f = 0.0;
        double dt = t - prevTime;  // prevTime starts at 0, same as the original
        if (dt <= 0.0 || std::isnan(dt)) dt = 1.0;
        double c = std::pow(4.0 / std::max(1.0, 2.0 / dt), p.kC);
        if (std::isnan(c) || std::isinf(c) || c < 0.0) c = 1.0;

        double b = w * mult;
        W[Base][i] = clean(b);
        W[Nerve][i] = clean(b * n);
        W[Fatigue][i] = clean(b * f);
        W[Cps][i] = clean(b * c);
        W[NF][i] = clean(b * n * f);
        W[NC][i] = clean(b * n * c);
        W[FC][i] = clean(b * f * c);
        W[All][i] = clean(b * n * f * c);
        prevTime = t;
    }

    // Expected total time to beat clicks [0..maxIdx] at precision L.
    auto evalTc = [&](size_t maxIdx, size_t startIdx, std::vector<double> const& Wm, double L) {
        if (std::isnan(L) || L <= 0.0) return 1e100;
        double r = 1.0, fails = 0.0;
        for (size_t j = startIdx; j <= maxIdx; j++) {
            double x = Wm[j] * L;
            if (std::isnan(x) || x > 6.0) continue;
            double q = erfcFast(x);
            double pj = std::max(1.0 - q, 1e-15);
            fails += T[j] * r * q;
            r *= pj;
            if (r < 1e-200) { r = 0.0; break; }
        }
        if (r <= 0.0 || std::isnan(r) || std::isnan(fails)) return 1e100;
        double res = (T[maxIdx] * r + fails) / r;
        return (std::isnan(res) || std::isinf(res)) ? 1e100 : res;
    };

    auto solve = [&](size_t maxIdx, size_t& startIdx, std::vector<double> const& Wm, double prevL) {
        if (std::isnan(prevL) || prevL < kMinLeft) prevL = kMinLeft;
        while (startIdx < maxIdx && Wm[startIdx] * prevL > 6.5) startIdx++;

        double L0 = prevL, f0 = evalTc(maxIdx, startIdx, Wm, L0) - p.target;
        double L1 = L0 * 1.001 + 0.001, f1 = evalTc(maxIdx, startIdx, Wm, L1) - p.target;
        double Ln = L1;
        bool ok = false;
        if (std::abs(f0) < 1e90 && std::abs(f1) < 1e90) {
            for (int it = 0; it < 10; it++) {
                double d = f1 - f0;
                if (std::abs(d) < 1e-9 || !std::isfinite(d)) break;
                double step = f1 * (L1 - L0) / d;
                if (!std::isfinite(step)) break;
                Ln = std::clamp(L1 - step, kMinLeft, kMaxRight);
                double fn = evalTc(maxIdx, startIdx, Wm, Ln) - p.target;
                if (std::isnan(fn)) break;
                if (std::abs(fn) < 1e-4) { ok = true; break; }
                L0 = L1; f0 = f1; L1 = Ln; f1 = fn;
            }
        }
        if (!ok) {
            double lo = prevL, hi = prevL + 10.0;
            for (int e = 0; e < 64 && evalTc(maxIdx, startIdx, Wm, hi) > p.target; e++) {
                hi *= 2.0;
                if (hi > kMaxRight) { hi = kMaxRight; break; }
            }
            for (int it = 0; it < 60; it++) {
                double mid = (lo + hi) * 0.5;
                if (evalTc(maxIdx, startIdx, Wm, mid) > p.target) lo = mid; else hi = mid;
                if (hi - lo < 1e-5 || (hi - lo) / (mid > 0 ? mid : 1.0) < 1e-5) break;
            }
            Ln = (lo + hi) * 0.5;
        }
        if (!std::isfinite(Ln)) return kMinLeft;
        return std::clamp(Ln, kMinLeft, kMaxRight);
    };

    size_t total = N * MetricCount, done = 0;
    for (int m = 0; m < MetricCount; m++) {
        auto& s = out.series[m];
        s.assign(N, 0.0);
        double prev = kMinLeft;
        size_t startIdx = 0;
        for (size_t i = 0; i < N; i++) {
            if (cancel && (i & 31) == 0 && cancel()) return out;
            bool lastInFrame = (i == N - 1) || clicks[i].frame != clicks[i + 1].frame;
            if (lastInFrame) {
                s[i] = solve(i, startIdx, W[m], prev);
                prev = s[i];
            }
            else {
                s[i] = prev;
            }
            if (progress && (++done % 64 == 0)) progress->store(float(done) / float(total));
        }
    }
    if (progress) progress->store(1.f);
    return out;
}

} // namespace afw::lstar
