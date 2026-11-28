#include "window_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace xfc {

int tierIndex(double w) {
    static const double bounds[kTierCount - 1] = {1.0, 2.0, 3.0, 5.0, 8.0, 15.0};
    for (int i = 0; i < kTierCount - 1; ++i)
        if (w <= bounds[i] + 1e-9) return i;
    return kTierCount - 1;
}

double findThreshold(const std::function<bool(double)>& ok, double maxShift,
                     double resolution, bool* capped) {
    if (capped) *capped = false;
    if (maxShift <= 0.0 || !ok(0.0)) return 0.0;
    if (ok(maxShift)) {
        if (capped) *capped = true;
        return maxShift;
    }
    double lo = 0.0, hi = maxShift;   // ok(lo) true, ok(hi) false
    resolution = std::max(resolution, 1e-6);
    while (hi - lo > resolution) {
        double mid = 0.5 * (lo + hi);
        if (ok(mid)) lo = mid; else hi = mid;
    }
    return lo;
}

Window measureWindow(const std::function<bool(double)>& okEarlier,
                     const std::function<bool(double)>& okLater,
                     double maxShift, double resolution) {
    Window w;
    bool c1 = false, c2 = false;
    w.earlier = findThreshold(okEarlier, maxShift, resolution, &c1);
    w.later = findThreshold(okLater, maxShift, resolution, &c2);
    w.capped = c1 || c2;
    return w;
}

std::string formatFrames(double f) {
    char buf[32];
    if (std::fabs(f - std::round(f)) < 0.0005)
        std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::lround(f)));
    else
        std::snprintf(buf, sizeof buf, "%.3f", f);
    return buf;
}

std::string formatMs(double f, int tps) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f ms", f * 1000.0 / (tps > 0 ? tps : 240));
    return buf;
}

} // namespace xfc
