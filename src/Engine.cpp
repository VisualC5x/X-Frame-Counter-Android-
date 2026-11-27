// Playback, xdBot link, cache and the trajectory-based analyser.
#include "XFC.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
#include <fstream>

namespace xfc {

// m_gameState.m_currentProgress: physics-step counter (same field xdBot reads in Global::getCurrentFrame).
int currentFrame(GJBaseGameLayer* gl) { return gl ? (int)gl->m_gameState.m_currentProgress : 0; }

static bool opt(const char* k) { return Mod::get()->getSettingValue<bool>(k); }

double activeTps() {
    auto& s = State::get();
    if (s.simulating) return 240.0 * (s.hiMode ? s.hiMul : 1);
    return opt("tps-enabled") ? (double)Mod::get()->getSettingValue<int64_t>("tps") : 240.0;
}

void applyFps() {
    if (!opt("fps-enabled")) return;
    double fps = (double)Mod::get()->getSettingValue<int64_t>("fps");
    CCDirector::get()->setAnimationInterval(1.0 / fps);
}

bool State::levelMatches(std::string& why) {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_level || macro.levelId <= 0) return true;
    if (pl->m_level->m_levelID.value() == macro.levelId) return true;
    why = fmt::format("This macro is for \"{}\" (id {}), you are in a different level", macro.levelName, macro.levelId);
    return false;
}

void State::analyzeAndPlay() {
    if (!windows.empty()) { startPlayback(); return; }
    playAfter = true; startAnalysis();
}

std::filesystem::path State::xfcDir() {
    auto d = Mod::get()->getSaveDir() / "macros";
    std::error_code ec; std::filesystem::create_directories(d, ec);
    return d;
}

bool State::xdbotLoaded() const { return Loader::get()->getLoadedMod("zilko.xdbot") != nullptr; }

std::filesystem::path State::xdbotFolder() {
    auto xd = Loader::get()->getLoadedMod("zilko.xdbot");
    if (!xd) return {};
    auto p = xd->getSettingValue<std::filesystem::path>("macros_folder");   // xdBot's own "Macro Save Location"
    if (p.empty()) p = xd->getSaveDir() / "macros";
    std::error_code ec; std::filesystem::create_directories(p, ec);
    return p;
}

bool State::sendToXdbot() {
    if (!macro.ok() || macroPath.empty()) { status = "Load a macro first"; return false; }
    auto dir = xdbotFolder();
    if (dir.empty()) { status = "xdBot is not installed / loaded"; return false; }
    std::error_code ec;
    std::filesystem::copy_file(macroPath, dir / macroName, std::filesystem::copy_options::overwrite_existing, ec);
    status = ec ? "Copy to xdBot failed: " + ec.message() : "Sent to xdBot - open xdBot > Load and pick " + macroName;
    return !ec;
}

std::vector<std::filesystem::path> State::findMacros() {
    std::vector<std::filesystem::path> out, dirs{xfcDir()};
    auto xf = xdbotFolder(); if (!xf.empty()) dirs.push_back(xf);
    for (auto& d : dirs) {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(d, ec))
            if (e.is_regular_file() && e.path().extension() == ".gdr") out.push_back(e.path());   // .gdr only
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(), [](auto& a, auto& b) { return a.filename() == b.filename(); }), out.end());
    return out;
}

void State::clearResults() { windows.clear(); clickInput.clear(); summary = {}; phase = Phase::Idle; progress = 0; }

bool State::loadMacro(const std::filesystem::path& p) {
    stopPlayback(); clearResults();
    auto m = loadMacroFile(p.string());
    if (!m.ok()) { status = m.error; return false; }
    macro = std::move(m); macroPath = p.string(); macroName = p.filename().string();
    status = macro.fps != 240.0 ? fmt::format("Warning: macro is {} fps, expected 240", macro.fps)
                                : fmt::format("{} - {} inputs - {}", macroName, macro.inputs.size(), macro.levelName);
    if (loadCache()) status += "  (analysis cached)";
    return true;
}

std::filesystem::path State::cachePath() const {
    auto d = Mod::get()->getSaveDir() / "cache";
    std::error_code ec; std::filesystem::create_directories(d, ec);
    return d / fmt::format("{}_{}_v2.xfc", macroName, macro.inputs.size());
}

void State::saveCache() {
    std::ofstream f(cachePath());
    for (size_t i = 0; i < windows.size(); i++) {
        auto& w = windows[i];
        f << clickInput[i] << ' ' << w.early << ' ' << w.late << ' ' << w.fracEarly << ' ' << w.fracLate << ' ' << w.blockedEarly << ' ' << w.blockedLate
          << ' ' << w.gapEarly << ' ' << w.gapLate << ' ' << w.baseDies << ' ' << w.capped << ' ' << w.frames << ' ' << w.tier << '\n';
    }
}

bool State::loadCache() {
    std::ifstream f(cachePath()); if (!f) return false;
    windows.clear(); clickInput.clear();
    size_t idx; Window w;
    while (f >> idx >> w.early >> w.late >> w.fracEarly >> w.fracLate >> w.blockedEarly >> w.blockedLate >> w.gapEarly >> w.gapLate >> w.baseDies >> w.capped >> w.frames >> w.tier) {
        clickInput.push_back(idx); windows.push_back(w);
    }
    summary = summarize(windows); phase = windows.empty() ? Phase::Idle : Phase::Done;
    return !windows.empty();
}

static void restartLevel(PlayLayer* pl) {
    if (pl->m_levelSettings->m_platformerMode) pl->resetLevelFromStart(); else pl->resetLevel();
}

void State::startPlayback() {
    if (!macro.ok() || macro.inputs.empty()) { status = "Load a .gdr macro first"; return; }
    auto pl = PlayLayer::get();
    if (!pl) { status = "Open the level first, then press Play"; return; }
    { std::string why; if (!levelMatches(why)) { status = why; return; } }
    run = macro.inputs; next = 0; nextClick = 0; playing = true;
    if (hud) hud->resetCounts();
    clearRings();
    restartLevel(pl);
    status = "Playing...";
}

void State::stopPlayback() { if (playing) { playing = false; status = "Stopped"; } }

void State::announce(size_t ordinal) {
    if (ordinal >= windows.size()) return;
    auto& w = windows[ordinal];
    bool sub = w.early + w.late + 1 < 2;
    double gap = std::min(w.gapEarly > 0 ? w.gapEarly : 1e9, w.gapLate > 0 ? w.gapLate : 1e9);
    if (hud) hud->onClick(w.tier, w.frames, w.ms(macro.fps), sub, gap > 1e8 ? -1 : gap);
    if (opt("show-rings")) if (auto pl = PlayLayer::get()) spawnRing(pl, w, macro.fps);
}

// ---------------------------------------------------------------- hooks

class $modify(XFCBase, GJBaseGameLayer) {
    // TPS control - same technique as xdBot's TPS bypass (hooks update + getModifiedDelta)
    void update(float dt) {
        double tps = activeTps();
        auto& s = State::get();
        if (tps == 240.0 || !PlayLayer::get() || s.simulating) return GJBaseGameLayer::update(dt);
        float newDt = (float)(1.0 / tps);
        float realDt = dt + m_fields->leftOver;
        if (realDt > dt && newDt < dt) realDt = dt;
        auto t0 = std::chrono::high_resolution_clock::now();
        int mult = static_cast<int>(realDt / newDt);
        for (int i = 0; i < mult; ++i) {
            GJBaseGameLayer::update(newDt);
            if (std::chrono::high_resolution_clock::now() - t0 > std::chrono::duration<double, std::milli>(16.666f)) { mult = i + 1; break; }
        }
        m_fields->leftOver += dt - newDt * mult;
    }

    float getModifiedDelta(float dt) {
        double tps = activeTps();
        if (tps == 240.0 || !PlayLayer::get()) return GJBaseGameLayer::getModifiedDelta(dt);
        float newDt = (float)(1.0 / tps);
        if (0 < m_resumeTimer) { m_resumeTimer--; dt = 0.0; }
        float warp = m_gameState.m_timeWarp <= 1.0 ? m_gameState.m_timeWarp : 1.0f;
        double sum = dt + m_extraDelta;
        float steps = std::round(sum / (warp * newDt));
        double used = steps * warp * newDt;
        m_extraDelta = sum - used;
        return used;
    }

    struct Fields { float leftOver = 0.f; };

    void processCommands(float dt) {
        auto& s = State::get();
        auto pl = PlayLayer::get();
        if (pl && this == (GJBaseGameLayer*)pl && !s.simulating && (s.playing || !s.windows.empty())) {
            int f = currentFrame(this);
            if (s.playing) {
                // own playback: inject inputs (same call xdBot makes) and announce clicks
                while (s.next < s.run.size() && (int)s.run[s.next].frame <= f) {
                    auto& in = s.run[s.next];
                    this->handleButton(in.down, in.button, in.player2);
                    if (in.down && !s.windows.empty()) {
                        auto it = std::find(s.clickInput.begin(), s.clickInput.end(), s.next);
                        if (it != s.clickInput.end()) s.announce(it - s.clickInput.begin());
                    }
                    s.next++;
                }
                if (s.next >= s.run.size() && f > (int)s.run.back().frame + 240) s.playing = false;
            } else if (opt("link-xdbot") && s.xdbotLoaded()) {
                // xdBot (or you) drives the inputs: XFC just follows the frame counter and announces each analysed click
                if (f < s.lastFrame || s.lastFrame < 0) { s.nextClick = 0; if (s.hud) s.hud->resetCounts(); clearRings(); }
                while (s.nextClick < s.clickInput.size() && (int)s.macro.inputs[s.clickInput[s.nextClick]].frame <= f) {
                    if ((int)s.macro.inputs[s.clickInput[s.nextClick]].frame >= s.lastFrame) s.announce(s.nextClick);
                    s.nextClick++;
                }
                s.lastFrame = f;
            }
        } else if (pl && this == (GJBaseGameLayer*)pl && s.simulating) {
            long f = s.hiMode ? s.hiStep : currentFrame(this);
            while (s.next < s.run.size() && (long)s.run[s.next].frame <= f) {
                auto& in = s.run[s.next++];
                this->handleButton(in.down, in.button, in.player2);
            }
        }
        GJBaseGameLayer::processCommands(dt);
    }
};

class $modify(XFCPlay, PlayLayer) {
    bool init(GJGameLevel* lvl, bool replay, bool dontCreate) {
        if (!PlayLayer::init(lvl, replay, dontCreate)) return false;
        auto& s = State::get();
        auto hud = Hud::create();
        hud->setZOrder(1000); this->addChild(hud);
        s.hud = hud; s.lastFrame = -1;
        applyFps();
        hud->setVisible(opt("show-hud"));
        return true;
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        auto& s = State::get();
        clearRings();
        if (!s.simulating && s.hud) { s.hud->resetCounts(); s.nextClick = 0; s.lastFrame = -1; }
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& s = State::get();
        if (s.hud && !s.simulating) s.hud->setFrame(currentFrame(this));
    }
    void onQuit() { auto& s = State::get(); s.hud = nullptr; s.playing = false; clearRings(); PlayLayer::onQuit(); }

    void destroyPlayer(PlayerObject* p, GameObject* hazard) {
        auto& s = State::get();
        if (!s.simulating) { PlayLayer::destroyPlayer(p, hazard); return; }
        if (s.simDied) return;
        s.simDied = true;
        s.simDeathFrac = 0; s.simGap = 0;
        // distance still left / overshoot: depth inside the hazard vs. distance travelled per physics step
        if (p && hazard) {
            auto a = p->GameObject::getObjectRect(), b = hazard->getObjectRect();
            double ox = std::min(a.getMaxX(), b.getMaxX()) - std::max(a.getMinX(), b.getMinX());
            double oy = std::min(a.getMaxY(), b.getMaxY()) - std::max(a.getMinY(), b.getMinY());
            double pen = std::max(0.0, std::min(ox, oy));
            double step = std::max(0.01, (double)std::abs(p->getPositionX() - s.simPrevX));
            s.simGap = pen;
            s.simDeathFrac = std::clamp(1.0 - pen / step, 0.0, 1.0);
        }
        // original not called: no death effect / respawn - the analyser restores a checkpoint instead.
        // Works for either player, so dual mode and every gamemode go through the real game physics.
    }
};

// ---------------------------------------------------------------- analyser

namespace {

struct Ckpt { int frame; CheckpointObject* obj; };

struct GDBackend : Backend {
    State& s; PlayLayer* pl; int maxShift, horizon;
    std::vector<Ckpt> ckpts;
    double frac[2] = {0, 0}, gap[2] = {0, 0};
    GDBackend(State& st, PlayLayer* p, int ms, int hz) : s(st), pl(p), maxShift(ms), horizon(hz) {}

    bool stepOne() {
        int f0 = currentFrame(pl);
        s.simPrevX = pl->m_player1->getPositionX();
        pl->m_extraDelta = 0;
        pl->update(kStep + 1e-6f);
        return currentFrame(pl) == f0 + 1;
    }

    std::vector<std::pair<int, bool>> heldAt(int frame) {      // (button, player2) held at `frame` in the recorded macro
        std::vector<std::pair<int, bool>> held;
        for (auto& in : s.macro.inputs) {
            if ((int)in.frame >= frame) break;
            auto it = std::find_if(held.begin(), held.end(), [&](auto& h) { return h.first == in.button && h.second == in.player2; });
            if (in.down && it == held.end()) held.push_back({in.button, in.player2});
            if (!in.down && it != held.end()) held.erase(it);
        }
        return held;
    }

    // spam: a click may only move strictly between the previous release and its own release
    bool allowed(size_t click, int shift) override {
        size_t ii = s.clickInput[click]; auto& me = s.macro.inputs[ii];
        long lo = -1, hi = 1L << 30;
        for (size_t j = ii; j-- > 0;) { auto& o = s.macro.inputs[j]; if (o.button == me.button && o.player2 == me.player2) { lo = o.frame; break; } }
        for (size_t j = ii + 1; j < s.macro.inputs.size(); j++) { auto& o = s.macro.inputs[j]; if (o.button == me.button && o.player2 == me.player2) { hi = o.frame; break; } }
        long t = (long)me.frame + shift;
        return t > lo && t < hi && t >= 0;
    }

    bool survives(size_t click, int shift) override {
        size_t ii = s.clickInput[click];
        int cf = (int)s.macro.inputs[ii].frame;
        int target = std::max(0, cf + shift);
        int startMin = std::max(0, cf - maxShift - 2);
        const Ckpt* ck = nullptr;
        for (auto& c : ckpts) { if (c.frame <= startMin) ck = &c; else break; }
        if (!ck) return true;                                   // before the first checkpoint: nothing to judge

        s.run = s.macro.inputs;
        s.run[ii].frame = (uint32_t)target;
        std::stable_sort(s.run.begin(), s.run.end(), [](auto& a, auto& b) { return a.frame < b.frame; });

        pl->loadFromCheckpoint(ck->obj);
        for (auto& h : heldAt(ck->frame)) pl->handleButton(true, h.first, h.second);
        s.next = 0;
        while (s.next < s.run.size() && (int)s.run[s.next].frame < ck->frame) s.next++;
        s.simDied = false; s.simDeathFrac = 0; s.simGap = 0;

        int end = cf + horizon;
        while (currentFrame(pl) < end && !s.simDied) if (!stepOne()) { s.simDied = true; s.simDeathFrac = 0; }
        if (s.simDied && shift != 0) { frac[shift < 0 ? 0 : 1] = s.simDeathFrac; gap[shift < 0 ? 0 : 1] = s.simGap; }
        return !s.simDied;
    }
    // ---- sub-frame: same checkpoint, but physics at 240*M TPS so the click can sit at 1/M-frame positions
    bool stepHi(int M) {
        s.simPrevX = pl->m_player1->getPositionX();
        pl->m_extraDelta = 0;
        pl->update((float)(1.0 / (240.0 * M)) + 1e-8f);
        s.hiStep++;
        return true;
    }
    bool hiSurvives(size_t click, long off, int M, double& deathFrac) {
        size_t ii = s.clickInput[click]; int cf = (int)s.macro.inputs[ii].frame;
        int startMin = std::max(0, cf - 2);
        const Ckpt* ck = nullptr;
        for (auto& c : ckpts) { if (c.frame <= startMin) ck = &c; else break; }
        if (!ck) return true;
        s.run = s.macro.inputs;
        for (auto& r : s.run) r.frame *= M;
        s.run[ii].frame = (uint32_t)std::max<long>(0, (long)cf * M + off);
        std::stable_sort(s.run.begin(), s.run.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
        s.hiMode = true; s.hiMul = M; s.hiStep = (long)ck->frame * M;
        pl->loadFromCheckpoint(ck->obj);
        for (auto& h : heldAt(ck->frame)) pl->handleButton(true, h.first, h.second);
        s.next = 0;
        while (s.next < s.run.size() && (long)s.run[s.next].frame < s.hiStep) s.next++;
        s.simDied = false; s.simDeathFrac = 0; s.simGap = 0;
        long end = (long)(cf + std::min(horizon, 24)) * M;
        while (s.hiStep < end && !s.simDied) stepHi(M);
        s.hiMode = false; s.hiMul = 1;
        deathFrac = s.simDeathFrac;
        return !s.simDied;
    }
    bool refineSub(size_t click, double& fe, double& fl) override {
        const int M = 64; double d = 0;
        if (!hiSurvives(click, 0, M, d)) return false;               // high-TPS base run must itself survive
        auto side = [&](int dir) -> double {
            if (!allowed(click, dir)) return 0.0;
            if (hiSurvives(click, (long)dir * M, M, d)) return 1.0;  // survives a full extra frame at high TPS
            long lo = 0, hi = M; double last = d;
            while (hi - lo > 1) {
                long mid = (lo + hi) / 2;
                if (hiSurvives(click, (long)dir * mid, M, d)) lo = mid; else { hi = mid; last = d; }
            }
            return (lo + last) / M;                                  // last = survivable part of the final failing 1/M step
        };
        fe = side(-1); fl = side(+1);
        return true;
    }
    double edgeFraction(size_t, int dir) override { return frac[dir < 0 ? 0 : 1]; }
    double edgeGap(size_t, int dir) override { return gap[dir < 0 ? 0 : 1]; }
};

struct Driver : CCNode {
    State& s; PlayLayer* pl; std::unique_ptr<GDBackend> be;
    size_t clickIdx = 0; int maxShift; bool recording = true;

    static Driver* create(PlayLayer* p) { auto r = new Driver(p); r->autorelease(); return r; }
    Driver(PlayLayer* p) : s(State::get()), pl(p) { maxShift = (int)Mod::get()->getSettingValue<int64_t>("max-shift"); }

    void begin() {
        be = std::make_unique<GDBackend>(s, pl, maxShift, (int)Mod::get()->getSettingValue<int64_t>("horizon"));
        s.clickInput.clear(); s.windows.clear(); s.summary = {};
        for (size_t i = 0; i < s.macro.inputs.size(); i++) if (s.macro.inputs[i].down) s.clickInput.push_back(i);
        s.run = s.macro.inputs; s.next = 0;
        s.simulating = true; s.simDied = false;
        restartLevel(pl);
        s.phase = Phase::Recording; s.status = "Phase 1/2: recording checkpoints...";
        this->schedule(schedule_selector(Driver::tick), 0.f);
    }

    void finish(Phase p, const std::string& msg) {
        s.simulating = false; s.phase = p; s.status = msg;
        for (auto& c : be->ckpts) c.obj->release();
        this->unschedule(schedule_selector(Driver::tick));
        if (p == Phase::Done) { s.summary = summarize(s.windows); s.saveCache(); }
        else { s.windows.clear(); s.clickInput.clear(); }
        s.hiMode = false; s.hiMul = 1;
        restartLevel(pl); this->removeFromParent();
        if (p == Phase::Done && s.playAfter) { s.playAfter = false; s.startPlayback(); }
        else if (p != Phase::Done) s.playAfter = false;
    }

    void tick(float) {
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(10)) {
            if (recording) {
                int f = currentFrame(pl);
                if (f % 120 == 0 && (be->ckpts.empty() || be->ckpts.back().frame != f)) {
                    auto c = pl->createCheckpoint(); c->retain(); be->ckpts.push_back({f, c});
                }
                if (!be->stepOne()) return finish(Phase::Failed, "Step desync - physics must run at 240 TPS; retry");
                if (s.simDied) return finish(Phase::Failed, "Macro dies on its own at frame " + std::to_string(f) + " - wrong level / version?");
                s.progress = 0.5f * f / std::max<int>(1, s.macro.inputs.back().frame);
                if (f >= (int)s.macro.inputs.back().frame) { recording = false; s.status = "Phase 2/2: measuring click windows..."; break; }
            } else {
                if (clickIdx >= s.clickInput.size()) return finish(Phase::Done, fmt::format("Done - {} clicks analysed", s.windows.size()));
                s.windows.push_back(analyzeClick(*be, clickIdx, maxShift));
                clickIdx++;
                s.progress = 0.5f + 0.5f * clickIdx / s.clickInput.size();
            }
        }
    }
};

} // namespace

void State::startAnalysis() {
    if (!macro.ok()) { status = "Load a .gdr macro first"; return; }
    auto pl = PlayLayer::get();
    if (!pl) { status = "Open the level first, then press Analyze"; return; }
    if (phase == Phase::Recording || phase == Phase::Scanning) return;
    { std::string why; if (!levelMatches(why)) { status = why; playAfter = false; return; } }
    stopPlayback();
    auto d = Driver::create(pl); pl->addChild(d); d->begin();
}

} // namespace xfc
