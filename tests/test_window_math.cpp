#include "../legacy/window_math.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace xfc;
static bool near(double a, double b, double e = 0.002) { return std::fabs(a - b) <= e; }
int main() {
    assert(tierIndex(0.001) == 0 && tierIndex(1.0) == 0);
    assert(tierIndex(1.5) == 1 && tierIndex(2.0) == 1);
    assert(tierIndex(3.0) == 2 && tierIndex(4.0) == 3 && tierIndex(5.0) == 3);
    assert(tierIndex(6.0) == 4 && tierIndex(8.0) == 4);
    assert(tierIndex(9.0) == 5 && tierIndex(15.0) == 5);
    assert(tierIndex(16.0) == 6 && tierIndex(30.0) == 6 && tierIndex(99.0) == 6);
    assert(near(findThreshold([](double s){ return s <= 2.347; }, 30, 0.001), 2.347));
    assert(near(findThreshold([](double s){ return s <= 0.0; }, 30, 0.001), 0.0));
    bool cap = false;
    assert(near(findThreshold([](double){ return true; }, 30, 0.001, &cap), 30) && cap);
    assert(findThreshold([](double){ return false; }, 30, 0.001) == 0.0);
    Window w = measureWindow([](double s){ return s <= 1.25; }, [](double s){ return s <= 2.0; }, 30);
    assert(near(w.width(), 3.25) && !w.capped);
    Window c = measureWindow([](double){ return true; }, [](double s){ return s <= 1; }, 30);
    assert(c.capped);
    assert(formatFrames(3.0) == "3" && formatFrames(2.3474) == "2.347");
    assert(formatMs(1.0, 240) == "4.167 ms" && formatMs(6.0) == "25.000 ms");
    std::puts("window_math: all tests passed");
}
