#pragma once
#include "gdr.hpp"
#include <vector>

namespace xfc {

struct ClickResult {
    int input = -1;          // index into the (sorted) input list
    int frame = 0;           // nominal frame of the click
    int earlier = 0;         // frames the click can move earlier and still survive
    int later = 0;           // frames the click can move later and still survive
    int count = 0;           // size of the window in frames = earlier + later + 1
    bool earlyLimited = false; // stopped by the neighbouring input (spam), not by a hazard
    bool lateLimited = false;
    bool capped = false;     // reached the max search distance, real window is bigger
    bool dies = false;       // the unmodified click already dies
    bool unstable = false;   // the verification pass got a different answer: restore/determinism problem
    bool paired = false;     // the release moved together with the click (hold length kept)
};

struct AnalysisOpts {
    bool pairRelease = false; // move the matching release together with the press
    bool verify = true;       // re-run both edges and the baseline once more
    int lookAhead = 3;        // keep checking until this many later clicks have been passed
    int spanCap = 6000;       // hard cap of the survive-check length (frames)
};

// Runs the level with the given inputs (any order) and reports whether the player
// is still alive when the game reaches `untilFrame`.
class ISim {
public:
    virtual ~ISim() = default;
    virtual bool survives(const std::vector<Input>& inputs, int untilFrame) = 0;
};

// Exact integer-frame window of a single click. Inputs must be sorted by frame.
ClickResult analyzeClick(ISim& sim, const std::vector<Input>& base, size_t index,
                         int maxShift, int horizon, const AnalysisOpts& opts = AnalysisOpts());

} // namespace xfc
