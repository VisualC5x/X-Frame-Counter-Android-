#include "analysis.hpp"
#include <algorithm>
#include <climits>

namespace xfc {

ClickResult analyzeClick(ISim& sim, const std::vector<Input>& base, size_t index,
                         int maxShift, int horizon, const AnalysisOpts& o) {
    ClickResult r;
    r.input = static_cast<int>(index);
    const Input orig = base[index];
    r.frame = orig.frame;
    auto same = [&](size_t k) { return base[k].button == orig.button && base[k].p2 == orig.p2; };

    // previous input of the same button limits how far the click may move earlier
    int lo = 0;
    for (size_t k = index; k-- > 0;)
        if (same(k)) { lo = base[k].frame + 1; break; }

    // matching release (only when the very next input of this button is a release)
    const size_t none = static_cast<size_t>(-1);
    size_t rel = none;
    if (o.pairRelease)
        for (size_t k = index + 1; k < base.size(); ++k)
            if (same(k)) { if (!base[k].down) rel = k; break; }
    r.paired = rel != none;

    // hi = last frame the press may reach
    int hi = INT_MAX;
    if (rel != none) {
        const int hold = base[rel].frame - orig.frame;
        for (size_t k = rel + 1; k < base.size(); ++k)
            if (same(k)) { hi = base[k].frame - 1 - hold; break; }
    } else {
        for (size_t k = index + 1; k < base.size(); ++k)
            if (same(k)) { hi = base[k].frame - 1; break; }
    }

    // A wrong click can kill long after it (a cube jump lasts >100 frames, an orb mistake much longer),
    // so keep checking until `lookAhead` later clicks are behind us, plus a margin.
    int span = horizon;
    {
        int seen = 0, far = 0;
        for (size_t k = index + 1; k < base.size() && seen < std::max(1, o.lookAhead); ++k)
            if (base[k].down && base[k].frame > orig.frame) { far = base[k].frame - orig.frame + 90; ++seen; }
        span = std::max(span, far);
    }
    span = std::min(span, std::max(60, o.spanCap));

    std::vector<Input> mod = base;
    auto trial = [&](int nf) {
        mod[index].frame = nf;
        if (rel != none) mod[rel].frame = base[rel].frame + (nf - orig.frame);
        return sim.survives(mod, std::max(nf, orig.frame) + span);
    };

    if (!trial(orig.frame)) { r.dies = true; r.count = 0; return r; }

    // 0 = stopped by a failing trial, 1 = stopped by the neighbouring input, 2 = search limit reached
    int lateEnd = 0, earlyEnd = 0;
    for (int k = 1; k <= maxShift; ++k) {
        int nf = orig.frame + k;
        if (nf > hi) { r.lateLimited = true; lateEnd = 1; break; }
        if (!trial(nf)) break;
        r.later = k;
        if (k == maxShift) { r.capped = true; lateEnd = 2; }
    }
    for (int k = 1; k <= maxShift; ++k) {
        int nf = orig.frame - k;
        if (nf < lo) { r.earlyLimited = true; earlyEnd = 1; break; }
        if (!trial(nf)) break;
        r.earlier = k;
        if (k == maxShift) { r.capped = true; earlyEnd = 2; }
    }
    r.count = r.earlier + r.later + 1;

    // verification pass: the same trials must give the same answers a second time
    if (o.verify) {
        bool ok = trial(orig.frame) && trial(orig.frame + r.later) && trial(orig.frame - r.earlier);
        if (ok && lateEnd == 0) ok = !trial(orig.frame + r.later + 1);
        if (ok && earlyEnd == 0) ok = !trial(orig.frame - r.earlier - 1);
        r.unstable = !ok;
    }
    return r;
}

} // namespace xfc
