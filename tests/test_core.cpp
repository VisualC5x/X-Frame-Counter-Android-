#include "../src/Analyzer.hpp"
#include "../src/GDR.hpp"
#include <cassert>
#include <cstdio>
#include <map>
using namespace xfc;
struct Fake : Backend {
    std::map<size_t, std::pair<int,int>> ok;   // click -> [lo, hi] survivable shifts
    std::map<size_t, double> fr;
    bool survives(size_t i, int s) override { auto [lo, hi] = ok[i]; return s >= lo && s <= hi; }
    double edgeFraction(size_t i, int) override { return fr[i]; }
};
int main() {
    Fake f; f.ok[0] = {0, 0}; f.fr[0] = 0.0;       // exact frame only, no sub-frame info
    f.ok[1] = {-1, 0};                              // 2 frames
    f.ok[2] = {-1, 1};                              // 3 frames
    f.ok[3] = {-3, 2};                              // 6 frames
    f.ok[4] = {-30, 30};                            // capped
    f.ok[5] = {1, 2};                               // recorded click dies
    f.ok[6] = {0, 0}; f.fr[6] = 0.6;                // sub-frame 0.6 per side -> clamped to 1
    auto w0 = analyzeClick(f, 0); assert(w0.frames == 0.001 && w0.tier == 0);
    auto w1 = analyzeClick(f, 1); assert(w1.frames == 2 && w1.tier == 1);
    auto w2 = analyzeClick(f, 2); assert(w2.frames == 3 && w2.tier == 2);
    auto w3 = analyzeClick(f, 3); assert(w3.frames == 6 && w3.tier == 4);
    auto w4 = analyzeClick(f, 4); assert(w4.capped && w4.tier == 6);
    auto w5 = analyzeClick(f, 5); assert(w5.baseDies);
    auto w6 = analyzeClick(f, 6); assert(w6.frames == 1.0 && w6.tier == 0);
    assert(w1.ms(240) > 8.3 && w1.ms(240) < 8.4);
    // spam: neighbour blocks the late side
    struct Spam : Backend { bool survives(size_t, int) override { return true; } bool allowed(size_t, int s) override { return s >= -2 && s <= 0; } } sp;
    auto w7 = analyzeClick(sp, 0); assert(w7.early == 2 && w7.late == 0 && w7.blockedLate && w7.frames == 3);
    return 0;
}
