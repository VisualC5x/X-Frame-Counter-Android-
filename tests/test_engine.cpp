#include "../legacy/engine.hpp"
#include "../legacy/gdr.hpp"
#include "../legacy/msgpack.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace xfc;

// Tiny fake game: cube jumps (vy=10) on a click while on the ground, gravity 1/step.
// Hazards: standing on the ground at frames 100..110 or 155..160 kills. Level ends at 200.
struct State { int f = 0, y = 0, vy = 0, junk = 0; };

struct FakeGame : IGame {
    State s;
    bool isDead = false, isDone = false;
    const std::vector<Input>* in = nullptr;
    size_t cur = 0;
    int live = 0;
    bool leaky = false;   // restore() forgets `junk`, like a game that does not restore everything

    void sync() { cur = 0; if (in) while (cur < in->size() && (*in)[cur].frame < s.f) ++cur; }
    void reset() override { s = State{}; isDead = isDone = false; sync(); }
    void stepOnce() override {
        if (in) {
            while (cur < in->size() && (*in)[cur].frame < s.f) ++cur;
            while (cur < in->size() && (*in)[cur].frame == s.f) {
                if ((*in)[cur].down && s.y == 0 && s.vy == 0) s.vy = 10;
                ++cur;
            }
        }
        if (s.y > 0 || s.vy > 0) { s.y += s.vy; s.vy -= 1; if (s.y <= 0) { s.y = 0; s.vy = 0; } }
        ++s.f;
        ++s.junk;
        if (((s.f >= 100 && s.f <= 110) || (s.f >= 155 && s.f <= 160)) && s.y == 0) isDead = true;
        if (s.f >= 200) isDone = true;
    }
    int frame() const override { return s.f; }
    void* checkpoint() override { ++live; return new State(s); }
    void restore(void* cp) override {
        int j = s.junk;
        s = *static_cast<State*>(cp);
        if (leaky) s.junk = j;
        sync();
    }
    uint64_t signature() const override {
        return static_cast<uint64_t>(s.f) * 1000003ULL ^ static_cast<uint64_t>(s.y) * 7919ULL ^
               static_cast<uint64_t>(s.vy + 50) * 104729ULL ^ static_cast<uint64_t>(s.junk) * 15485863ULL;
    }
    void release(void* cp) override { --live; delete static_cast<State*>(cp); }
    bool dead() const override { return isDead; }
    bool done() const override { return isDone; }
    void clearFlags() override { isDead = isDone = false; }
    void setInputs(const std::vector<Input>* v) override { in = v; sync(); }
};

// brute force: does a single click at frame j (+release at 130, second click handled separately) survive?
static bool survivesBrute(const std::vector<Input>& inputs) {
    FakeGame g;
    std::vector<Input> sorted = inputs;
    std::stable_sort(sorted.begin(), sorted.end(), [](Input const& a, Input const& b) { return a.frame < b.frame; });
    g.reset();
    g.setInputs(&sorted);
    while (!g.dead() && !g.done()) g.stepOnce();
    return !g.dead();
}

static std::vector<uint8_t> enc(std::initializer_list<uint8_t> v) { return std::vector<uint8_t>(v); }

int main() {
    // ---- engine vs brute force on a 2-click macro ----
    Macro m;
    m.inputs = {{95, 1, false, true}, {130, 1, false, false}, {150, 1, false, true}, {170, 1, false, false}};
    assert(survivesBrute(m.inputs));

    auto brute = [&](size_t idx) {
        int f = m.inputs[idx].frame, lo = f, hi = f;
        while (true) { auto v = m.inputs; v[idx].frame = lo - 1; if (lo - 1 < 0 || !survivesBrute(v)) break; --lo; }
        while (true) { auto v = m.inputs; v[idx].frame = hi + 1; if (!survivesBrute(v)) break; ++hi; }
        return std::pair<int, int>{f - lo, hi - f};
    };

    FakeGame game;
    Engine eng;
    EngineConfig cfg;
    cfg.horizon = 10;   // deliberately tiny: the next-click span must still give exact windows
    std::string err;
    assert(eng.start(&game, m, cfg, err));
    for (int guard = 0; guard < 100000 && eng.state() == Engine::State::Running; ++guard) eng.work(0.001);
    assert(eng.state() == Engine::State::Done);
    assert(eng.results().size() == 2);
    auto b0 = brute(0), b1 = brute(2);
    auto const& r0 = eng.results()[0];
    auto const& r1 = eng.results()[1];
    std::printf("click1 earlier=%d later=%d count=%d (brute %d/%d)\n", r0.earlier, r0.later, r0.count, b0.first, b0.second);
    std::printf("click2 earlier=%d later=%d count=%d (brute %d/%d)\n", r1.earlier, r1.later, r1.count, b1.first, b1.second);
    assert(r0.earlier == b0.first && r0.later == b0.second && r0.count == b0.first + b0.second + 1);
    assert(r1.earlier == b1.first && r1.later == b1.second);
    assert(game.live == 0);   // every snapshot released
    assert(eng.resultOfInput(0) == 0 && eng.resultOfInput(2) == 1 && eng.resultOfInput(1) == -1);

    // ---- no drift, nothing unstable on a clean game ----
    assert(eng.driftCount() == 0 && eng.unstableCount() == 0);

    // ---- paired release: press and release move together, compared with brute force ----
    {
        auto bruteP = [&](size_t idx) {
            int f = m.inputs[idx].frame, lo = f, hi = f;
            while (true) {
                auto v = m.inputs; v[idx].frame = lo - 1; v[idx + 1].frame = m.inputs[idx + 1].frame - (f - lo + 1);
                if (lo - 1 < 0 || !survivesBrute(v)) break;
                --lo;
            }
            while (true) {
                auto v = m.inputs; v[idx].frame = hi + 1; v[idx + 1].frame = m.inputs[idx + 1].frame + (hi + 1 - f);
                if (!survivesBrute(v)) break;
                ++hi;
            }
            return std::pair<int, int>{f - lo, hi - f};
        };
        FakeGame gp; Engine ep;
        EngineConfig cp = cfg; cp.pairRelease = true;
        assert(ep.start(&gp, m, cp, err));
        for (int guard = 0; guard < 100000 && ep.state() == Engine::State::Running; ++guard) ep.work(0.001);
        assert(ep.state() == Engine::State::Done);
        auto p0 = bruteP(0), p1 = bruteP(2);
        std::printf("paired click1 %d/%d (brute %d/%d)  click2 %d/%d (brute %d/%d)\n",
                    ep.results()[0].earlier, ep.results()[0].later, p0.first, p0.second,
                    ep.results()[1].earlier, ep.results()[1].later, p1.first, p1.second);
        assert(ep.results()[0].paired && ep.results()[0].earlier == p0.first && ep.results()[0].later == p0.second);
        assert(ep.results()[1].paired && ep.results()[1].earlier == p1.first && ep.results()[1].later == p1.second);
        assert(ep.unstableCount() == 0 && gp.live == 0);
    }

    // ---- a game whose restore is not exact is reported as drift ----
    {
        FakeGame gl; gl.leaky = true; Engine el;
        assert(el.start(&gl, m, cfg, err));
        for (int guard = 0; guard < 100000 && el.state() == Engine::State::Running; ++guard) el.work(0.001);
        assert(el.state() == Engine::State::Done && el.driftCount() > 0);
        assert(gl.live == 0);
    }

    // ---- verification pass flags a non-deterministic simulator ----
    {
        struct Flaky : ISim {
            int calls = 0;
            bool survives(const std::vector<Input>&, int) override { ++calls; return calls <= 3 || calls % 2 == 0; }
        } fl;
        std::vector<Input> base = {{50, 1, false, true}, {60, 1, false, false}};
        AnalysisOpts ao; ao.verify = true;
        ClickResult fr = analyzeClick(fl, base, 0, 5, 20, ao);
        assert(fr.unstable);
    }

    // ---- click that already dies inside its horizon ----
    Macro bad;
    bad.inputs = {{40, 1, false, true}, {60, 1, false, false}};
    FakeGame g2; Engine e2;
    EngineConfig cfgLong = cfg; cfgLong.horizon = 120;
    assert(e2.start(&g2, bad, cfgLong, err));
    for (int guard = 0; guard < 100000 && e2.state() == Engine::State::Running; ++guard) e2.work(0.001);
    assert(e2.state() == Engine::State::Done && e2.results()[0].dies);
    assert(g2.live == 0);

    // ---- baseline dies before the click is reached -> error ----
    Macro bad2;
    bad2.inputs = {{130, 1, false, true}, {150, 1, false, false}};
    FakeGame g4; Engine e4;
    assert(e4.start(&g4, bad2, cfg, err));
    for (int guard = 0; guard < 100000 && e4.state() == Engine::State::Running; ++guard) e4.work(0.001);
    assert(e4.state() == Engine::State::Error);
    assert(e4.error().find("dies on its own") != std::string::npos);
    assert(g4.live == 0);

    // ---- spam: neighbouring release limits the window ----
    Macro sp;
    sp.inputs = {{95, 1, false, true}, {97, 1, false, false}, {98, 1, false, true}, {130, 1, false, false}, {150, 1, false, true}, {170, 1, false, false}};
    FakeGame g3; Engine e3;
    assert(e3.start(&g3, sp, cfg, err));
    for (int guard = 0; guard < 100000 && e3.state() == Engine::State::Running; ++guard) e3.work(0.001);
    assert(e3.state() == Engine::State::Done);
    // first click: later shift stopped by its release at 97 (hi = 96)
    assert(e3.results()[0].lateLimited || e3.results()[0].later <= 1);

    // ---- msgpack / gdr ----
    // {"framerate":240,"level":{"id":5},"inputs":[{"frame":10,"btn":1,"2p":false,"down":true}]}
    std::vector<uint8_t> b = {0x83,
        0xa9,'f','r','a','m','e','r','a','t','e', 0xcc, 240,
        0xa5,'l','e','v','e','l', 0x81, 0xa2,'i','d', 0x05,
        0xa6,'i','n','p','u','t','s', 0x91, 0x84,
            0xa5,'f','r','a','m','e', 0x0a,
            0xa3,'b','t','n', 0x01,
            0xa2,'2','p', 0xc2,
            0xa4,'d','o','w','n', 0xc3};
    Macro pm;
    assert(parseGdr(b, pm, err));
    assert(pm.fps == 240 && pm.levelId == 5 && pm.inputs.size() == 1);
    assert(pm.inputs[0].frame == 10 && pm.inputs[0].down && !pm.inputs[0].p2);
    std::vector<uint8_t> trunc(b.begin(), b.begin() + 20);
    assert(!parseGdr(trunc, pm, err));
    assert(!parseGdr(enc({0x91, 0x01}), pm, err) && err.find("map expected") != std::string::npos);
    std::puts("engine/gdr: all tests passed");
}
