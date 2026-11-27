#pragma once
// XFC core: turns "does the run survive if this click is N frames early/late?" answers
// into a click window (frames / ms) and one of 7 colour tiers. Pure C++, no GD dependency,
// so it is unit-tested off-game (tests/test_core.cpp).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xfc {

struct Tier { double maxFrames; const char* label; uint8_t r, g, b; };

// 7 tiers, tightest -> loosest (max window 30 frames). Colours are defaults; the mod lets users override.
inline constexpr std::array<Tier, 7> kTiers{{
    {1.0,  "0.001-1", 255,  45,  60},
    {2.0,  "2",     255, 140,  30},
    {3.0,  "3",     255, 225,  40},
    {5.0,  "4-5",    80, 230,  90},
    {8.0,  "6-8",    40, 220, 255},
    {15.0, "9-15",   50, 130, 255},
    {30.0, "16-30", 170,  90, 255},
}};

inline int tierFor(double frames) {
    for (size_t i = 0; i < kTiers.size(); i++) if (frames <= kTiers[i].maxFrames) return (int)i;
    return (int)kTiers.size() - 1;
}

struct Backend {
    virtual ~Backend() = default;
    // Does the run survive if click `idx` is moved by `shift` frames (negative = earlier)?
    virtual bool survives(size_t idx, int shift) = 0;
    // Fraction (0..1) of one extra frame that would still be survivable past the last good shift
    // in direction `dir` (-1 / +1), estimated from hazard penetration at the first failing shift.
    virtual double edgeFraction(size_t idx, int dir) { (void)idx; (void)dir; return 0.0; }
    // Spam support: false if moving click `idx` by `shift` would cross a neighbouring input
    // (its own release or the previous release) - the window is then limited by the neighbour, not a hazard.
    virtual bool allowed(size_t idx, int shift) { (void)idx; (void)shift; return true; }
    // Hazard penetration (game units) at the first failing shift in direction dir; 0 if unknown.
    virtual double edgeGap(size_t idx, int dir) { (void)idx; (void)dir; return 0.0; }
    // Sub-frame refinement: re-simulate at a much higher TPS and bisect how far (in frames, 0..1) the click can
    // move earlier (fe) / later (fl) inside one frame. Returns false if it cannot (then edgeFraction is used).
    virtual bool refineSub(size_t idx, double& fe, double& fl) { (void)idx; (void)fe; (void)fl; return false; }
};

struct Window {
    int    early = 0, late = 0;       // extra survivable frames before / after the recorded frame
    double fracEarly = 0, fracLate = 0;
    bool   blockedEarly = false, blockedLate = false;   // limited by a neighbouring input (spam), not a hazard
    double gapEarly = 0, gapLate = 0;                    // hazard penetration at the first failing shift (units)
    bool   baseDies = false;          // the recorded click itself does not survive (desync)
    bool   capped = false;            // hit maxShift on a side
    double frames = 0;                // reported window, in frames
    int    tier = 0;
    double ms(double fps) const { return frames / fps * 1000.0; }
};

inline Window analyzeClick(Backend& b, size_t idx, int maxShift = 30) {
    Window w;
    if (!b.survives(idx, 0)) { w.baseDies = true; w.frames = 0; w.tier = 0; return w; }
    for (int s = 1; s <= maxShift; s++) {
        if (!b.allowed(idx, -s)) { w.blockedEarly = true; break; }
        if (b.survives(idx, -s)) w.early++; else break;
    }
    for (int s = 1; s <= maxShift; s++) {
        if (!b.allowed(idx, s)) { w.blockedLate = true; break; }
        if (b.survives(idx, s)) w.late++; else break;
    }
    w.capped = (w.early >= maxShift) || (w.late >= maxShift);
    if (w.early < maxShift && !w.blockedEarly) { w.fracEarly = std::clamp(b.edgeFraction(idx, -1), 0.0, 1.0); w.gapEarly = b.edgeGap(idx, -1); }
    if (w.late  < maxShift && !w.blockedLate)  { w.fracLate  = std::clamp(b.edgeFraction(idx, +1), 0.0, 1.0); w.gapLate  = b.edgeGap(idx, +1); }
    int count = w.early + w.late + 1;                       // discrete frames that work
    if (count == 1) {
        double fe = 0, fl = 0;
        if (b.refineSub(idx, fe, fl)) { w.fracEarly = w.blockedEarly ? 0 : std::clamp(fe, 0.0, 1.0); w.fracLate = w.blockedLate ? 0 : std::clamp(fl, 0.0, 1.0); }
    }
    w.frames = count >= 2 ? (double)count
                          : std::clamp(w.fracEarly + w.fracLate, 0.001, 1.0);   // sub-frame (0.001 - 1), 3 decimals
    w.tier = tierFor(w.frames);
    return w;
}

struct Summary {
    std::array<int, 7> perTier{};
    int total = 0;
    double tightest = 1e9;
};

inline Summary summarize(const std::vector<Window>& ws) {
    Summary s;
    for (auto& w : ws) { s.perTier[w.tier]++; s.total++; s.tightest = std::min(s.tightest, w.frames); }
    return s;
}

} // namespace xfc
