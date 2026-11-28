#pragma once
#include <functional>
#include <string>

namespace xfc {

// Click window in frames (1 frame = 1 physics step at 240 TPS).
// earlier/later = how far the click can move before the attempt fails.
struct Window {
    double earlier = 0.0;
    double later = 0.0;
    bool capped = false;               // search limit reached, true window is bigger
    double width() const { return earlier + later; }
};

constexpr int kTierCount = 7;

// 0..6 = tier 1..7. Upper bounds in frames: 1, 2, 3, 5, 8, 15, 30 (anything above = tier 7, capped).
int tierIndex(double widthFrames);

// Largest shift s in [0, maxShift] with ok(s) == true. Requires ok(0) == true and ok monotone.
// capped is set when ok(maxShift) is true.
double findThreshold(const std::function<bool(double)>& ok, double maxShift,
                     double resolution, bool* capped = nullptr);

Window measureWindow(const std::function<bool(double)>& okEarlier,
                     const std::function<bool(double)>& okLater,
                     double maxShift, double resolution = 0.001);

std::string formatFrames(double frames);               // "3" or "2.347"
std::string formatMs(double frames, int tps = 240);    // "12.500 ms"

} // namespace xfc
