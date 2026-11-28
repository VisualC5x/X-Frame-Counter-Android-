// Playback, xdBot link, cache and the trajectory-based analyser.
#include "XFC.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <map>
#include <algorithm>
#include <fstream>
#include <cmath>

namespace xfc {

// Frame = the game's own physics-step counter (GJGameState::m_currentProgress): the same number xdBot stamps on every
// input, +1 per 1/240 s step, 0 on a fresh attempt, saved/restored with every checkpoint, unaffected by timewarp / TPS.
int currentFrame(GJBaseGameLayer* gl) { return gl ? gl->m_gameState.m_currentProgress : 0; }

static bool opt(const char* k) { return optB(k); }

// Runs a callback once, a moment later, on the running scene (used to start things after the pause menu closed / a level loaded).
namespace {
struct Later : CCNode {
    std::function<void()> fn;
    static void run(CCNode* parent, float delay, std::function<void()> f) {
        if (!parent) return;
        auto n = new Later();
        if (!n->init()) { delete n; return; }
        n->autorelease(); n->fn = std::move(f);
        parent->addChild(n);
        n->scheduleOnce(schedule_selector(Later::fire), delay);
    }
    void fire(float) { auto f = std::move(fn); this->removeFromParent(); if (f) f(); }
};
// close the pause menu if the level is paused; returns true when it was paused
bool resumeIfPaused(PlayLayer* pl) {
    if (!pl || !pl->m_isPaused) return false;
    if (auto scene = CCDirector::get()->getRunningScene())
        if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
    return true;
}
}

double activeTps() {
    auto& s = State::get();
    if (s.simulating) return 240.0 * (s.hiMode ? s.hiMul : 1);
    return opt("tps-enabled") ? (double)optI("tps") : 240.0;
}

void applyFps() {
    if (!opt("fps-enabled")) return;
    double fps = (double)optI("fps");
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

bool State::enterLevel(int action) {
    if (macro.levelId <= 0) { status = "Open the macro's level first, then press the button"; return false; }
    auto lvl = GameLevelManager::get()->getSavedLevel(macro.levelId);
    if (!lvl) { status = fmt::format("Level {} is not saved on this device - open it once from the level list, then press again", macro.levelId); return false; }
    pending = action; starting = true;
    status = fmt::format("Opening level \"{}\"...", lvl->m_levelName.c_str());
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, PlayLayer::scene(lvl, false, false)));
    return true;
}

void onLevelReady(PlayLayer* pl) {
    auto& s = State::get();
    int action = s.pending; s.pending = 0;
    if (!action || !pl) { s.starting = false; return; }
    Later::run(pl, 0.7f, [action] {
        auto& st = State::get(); st.starting = false;
        if (!PlayLayer::get()) return;
        if (action == 1) st.startAnalysis(); else st.analyzeAndPlay();
    });
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
    if (!m.ok()) {
        status = m.error.empty() ? "Failed to load macro" : m.error;
        log::error("XFC loadMacro failed: {} ({})", status, p.string());
        return false;
    }
    macro = std::move(m);
    macroPath = p.string();
    macroName = p.filename().string();

    // No made-up windows: click windows come only from the real physics analysis (or its cache).
    windows.clear(); clickInput.clear(); summary = {}; phase = Phase::Idle; progress = 0;
    status = fmt::format("{}  |  {} inputs  |  not analysed yet", macroName, macro.inputs.size());
    if (macro.fps > 1 && std::abs(macro.fps - 240.0) > 0.5)
        status += fmt::format("  (fps {:.0f})", macro.fps);

    if (loadCache()) {
        progress = 1.f;
        status = fmt::format("{}  |  {} inputs  |  {} clicks  |  tightest {:.3f}f  [cache]", macroName, macro.inputs.size(), summary.total, summary.tightest);
        if (macro.fps > 1 && std::abs(macro.fps - 240.0) > 0.5) status += fmt::format("  (fps {:.0f})", macro.fps);
    }
    log::info("XFC loaded {} — {} inputs, {} click windows", macroName, macro.inputs.size(), windows.size());
    return true;
}

std::filesystem::path State::cachePath() const {
    auto d = Mod::get()->getSaveDir() / "cache";
    std::error_code ec; std::filesystem::create_directories(d, ec);
    return d / fmt::format("{}_{}_v4.xfc", macroName, macro.inputs.size());
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
        if (idx >= macro.inputs.size()) { windows.clear(); clickInput.clear(); return false; }   // stale / foreign cache
        w.tier = std::clamp(w.tier, 0, (int)kTierCount - 1);
        clickInput.push_back(idx); windows.push_back(w);
    }
    summary = summarize(windows); phase = windows.empty() ? Phase::Idle : Phase::Done;
    return !windows.empty();
}

static void restartLevel(PlayLayer* pl) {
    // a macro always starts at the very beginning - not at a practice checkpoint
    if (pl->m_levelSettings->m_platformerMode || pl->m_isPracticeMode) pl->resetLevelFromStart(); else pl->resetLevel();
}

void State::startPlayback() {
    if (!macro.ok() || macro.inputs.empty()) { status = "Load a .gdr macro first"; return; }
    auto pl = PlayLayer::get();
    if (!pl) { enterLevel(2); return; }
    { std::string why; if (!levelMatches(why)) { status = why; return; } }
    auto begin = [] {
        auto& s = State::get(); s.starting = false;
        auto pl = PlayLayer::get();
        if (!pl) return;
        s.run = s.macro.inputs; s.next = 0; s.nextClick = 0; s.playing = true;
        if (s.hud) s.hud->resetCounts();
        clearRings();
        restartLevel(pl);
        s.status = "Playing...";
    };
    if (resumeIfPaused(pl)) {                    // started from the pause menu: close it first, the paused game would never run the macro
        starting = true; status = "Starting...";
        Later::run(CCDirector::get()->getRunningScene(), 0.4f, begin);
        return;
    }
    begin();
}

void State::stopPlayback() { if (playing) { playing = false; status = "Stopped"; } }

void State::announce(size_t ordinal) {
    if (ordinal >= windows.size()) return;
    auto& w = windows[ordinal];
    bool sub = w.early + w.late + 1 < 2;
    double gap = std::min(w.gapEarly > 0 ? w.gapEarly : 1e9, w.gapLate > 0 ? w.gapLate : 1e9);
    playTierSound(w.tier);
    if (hud) hud->onClick(w.tier, w.frames, w.ms(macro.fps), sub, gap > 1e8 ? -1 : gap);
    if (opt("show-rings")) if (auto pl = PlayLayer::get()) {
        auto& in = macro.inputs[clickInput[ordinal]];
        spawnRing(pl, (int)in.frame, true, in.player2, &w, macro.fps);
    }
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
                    this->handleButton(in.down, in.button, !in.player2);
                    if (!in.down && opt("show-rings") && opt("show-release-rings")) spawnRing(pl, (int)in.frame, false, in.player2, nullptr, s.macro.fps);
                    if (in.down && !s.windows.empty()) {
                        auto it = std::find(s.clickInput.begin(), s.clickInput.end(), s.next);
                        if (it != s.clickInput.end()) s.announce(it - s.clickInput.begin());
                    }
                    s.next++;
                }
                if (s.next >= s.run.size() && f > (int)s.run.back().frame + 240) s.playing = false;
            } else if (opt("link-xdbot") && s.xdbotLoaded()) {
                // xdBot (or you) drives the inputs: XFC just follows the frame counter and announces each analysed click
                if (f < s.lastFrame || s.lastFrame < 0) { s.nextClick = 0; s.nextInput = 0; if (s.hud) s.hud->resetCounts(); clearRings(); }
                while (s.nextClick < s.clickInput.size() && (int)s.macro.inputs[s.clickInput[s.nextClick]].frame <= f) {
                    if ((int)s.macro.inputs[s.clickInput[s.nextClick]].frame >= s.lastFrame) s.announce(s.nextClick);
                    s.nextClick++;
                }
                while (s.nextInput < s.macro.inputs.size() && (int)s.macro.inputs[s.nextInput].frame <= f) {
                    auto& in = s.macro.inputs[s.nextInput];
                    if (!in.down && (int)in.frame >= s.lastFrame && opt("show-rings") && opt("show-release-rings")) spawnRing(pl, (int)in.frame, false, in.player2, nullptr, s.macro.fps);
                    s.nextInput++;
                }
                s.lastFrame = f;
            }
        } else if (pl && this == (GJBaseGameLayer*)pl && s.simulating) {
            long f = s.hiMode ? s.hiStep : currentFrame(this);
            while (s.next < s.run.size() && (long)s.run[s.next].frame <= f) {
                auto& in = s.run[s.next++];
                this->handleButton(in.down, in.button, !in.player2);
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
        if (this->m_uiLayer) this->m_uiLayer->addChild(hud, 9999); else this->addChild(hud, 9999);
        s.hud = hud; s.lastFrame = -1;
        applyFps();
        hud->setVisible(opt("show-hud"));
        return true;
    }
    void loadFromCheckpoint(CheckpointObject* c) {
        PlayLayer::loadFromCheckpoint(c);
        if (c) m_gameState.m_currentProgress = c->m_gameState.m_currentProgress;   // frame always matches the checkpoint
    }
    void levelComplete() {
        auto& s = State::get();
        if (s.simulating) { s.simDone = true; return; }                            // analysis reached the end: no end screen
        PlayLayer::levelComplete();
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        auto& s = State::get();
        clearRings();
        if (!s.simulating && s.hud) { s.hud->resetCounts(); s.nextClick = 0; s.nextInput = 0; s.lastFrame = -1; }
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& s = State::get();
        if (s.hud && !s.simulating) s.hud->setFrame(currentFrame(this));
    }
    void onQuit() {
        auto& s = State::get();
        s.hud = nullptr; s.playing = false; s.starting = false; s.pending = 0; s.playAfter = false;
        s.simulating = false; s.hiMode = false; s.hiMul = 1; s.simDied = false; s.simDone = false;
        if (s.phase == Phase::Recording || s.phase == Phase::Scanning) s.phase = Phase::Idle;
        clearRings(); stopTierSounds();
        PlayLayer::onQuit();
    }

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
        if (s.simDone) return true;                          // level finished inside the simulation: nothing left to step
        int f0 = currentFrame(pl);
        s.simPrevX = pl->m_player1->getPositionX();
        for (int tries = 0; tries < 400; tries++) {          // the game swallows the first updates after a restart
            pl->m_extraDelta = 0;
            pl->update(kStep + 1e-6f);
            if (currentFrame(pl) > f0 || s.simDied || s.simDone) return true;
        }
        return false;
    }

    // After loading a checkpoint the game still remembers which buttons the PREVIOUS (failed) run was holding, and a
    // checkpoint stores no button state. Put the held-state back to exactly what the macro has at that frame - written
    // straight into the players so no extra click is generated - and drop commands queued by the old run.
    void syncHeld(int frame) {
        pl->m_queuedButtons.clear();
        for (PlayerObject* p : {pl->m_player1, pl->m_player2}) {
            if (!p) continue;
            for (int b = 1; b <= 3; b++) p->m_holdingButtons[b] = false;
        }
        for (auto& h : heldAt(frame)) {                      // h.second = player2 flag
            PlayerObject* p = h.second ? pl->m_player2 : pl->m_player1;
            if (p) p->m_holdingButtons[h.first] = true;
        }
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
        syncHeld(ck->frame);
        s.next = 0;
        while (s.next < s.run.size() && (int)s.run[s.next].frame < ck->frame) s.next++;
        s.simDied = false; s.simDone = false; s.simDeathFrac = 0; s.simGap = 0;

        int end = cf + horizon;
        while (currentFrame(pl) < end && !s.simDied && !s.simDone) if (!stepOne()) { s.simDied = true; s.simDeathFrac = 0; }
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
        syncHeld(ck->frame);
        s.next = 0;
        while (s.next < s.run.size() && (long)s.run[s.next].frame < s.hiStep) s.next++;
        s.simDied = false; s.simDone = false; s.simDeathFrac = 0; s.simGap = 0;
        long end = (long)(cf + std::min(horizon, 24)) * M;
        while (s.hiStep < end && !s.simDied && !s.simDone) stepHi(M);
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
    ~Driver() override {                      // level closed in the middle of an analysis: never leave the game in "simulating" mode
        if (be) { for (auto& c : be->ckpts) c.obj->release(); be->ckpts.clear(); }
        if (s.simulating) { s.simulating = false; s.hiMode = false; s.hiMul = 1; s.simDied = false; if (s.phase == Phase::Recording || s.phase == Phase::Scanning) s.phase = Phase::Idle; }
    }
    Driver(PlayLayer* p) : s(State::get()), pl(p) { maxShift = (int)optI("max-shift"); }

    void startLater(float) { this->unschedule(schedule_selector(Driver::startLater)); begin(); }
    int tickCount = 0;

    void begin() {
        s.starting = false;
        be = std::make_unique<GDBackend>(s, pl, maxShift, (int)optI("horizon"));
        s.clickInput.clear(); s.windows.clear(); s.summary = {};
        for (size_t i = 0; i < s.macro.inputs.size(); i++) if (s.macro.inputs[i].down) s.clickInput.push_back(i);
        s.run = s.macro.inputs; s.next = 0;
        s.simulating = true; s.simDied = false; s.simDone = false;
        restartLevel(pl);
        s.phase = Phase::Recording; s.status = "Phase 1/2: recording checkpoints...";
        if (s.hud) s.hud->setStatus("XFC: " + s.status, {140, 220, 255});
        this->schedule(schedule_selector(Driver::tick), 0.f);
    }

    void finish(Phase p, const std::string& msg) {
        s.simulating = false; s.phase = p; s.status = msg;
        for (auto& c : be->ckpts) c.obj->release();
        be->ckpts.clear();
        this->unschedule(schedule_selector(Driver::tick));
        if (p == Phase::Done) { s.summary = summarize(s.windows); s.saveCache(); }
        else { s.windows.clear(); s.clickInput.clear(); }
        s.hiMode = false; s.hiMul = 1;
        restartLevel(pl);
        if (s.hud) s.hud->setStatus("XFC: " + msg, p == Phase::Done ? ccColor3B{90, 255, 130} : ccColor3B{255, 90, 100}, p == Phase::Done ? 4.f : 12.f);
        this->removeFromParent();
        if (p == Phase::Done && s.playAfter) { s.playAfter = false; s.startPlayback(); }
        else if (p != Phase::Done) s.playAfter = false;
    }

    void tick(float) {
        if (s.hud && (++tickCount % 6) == 0)
            s.hud->setStatus(fmt::format("XFC analysing {:.0f}%  -  {}", s.progress * 100.f, recording ? "recording" : "measuring clicks"), {140, 220, 255});
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(10)) {
            if (recording) {
                int f = currentFrame(pl);
                if (f % 120 == 0 && (be->ckpts.empty() || be->ckpts.back().frame != f)) {
                    auto c = pl->createCheckpoint(); c->retain(); be->ckpts.push_back({f, c});
                }
                if (!be->stepOne()) return finish(Phase::Failed, "Step desync - physics must run at 240 TPS; retry");
                if (s.simDied) return finish(Phase::Failed, "The macro dies at frame " + std::to_string(f) + " in XFC's re-simulation - wrong level / different game version / macro not recorded at 240 TPS");
                s.progress = 0.5f * f / std::max<int>(1, s.macro.inputs.back().frame);
                if (s.simDone || f >= (int)s.macro.inputs.back().frame) { recording = false; s.simDone = false; s.status = "Phase 2/2: measuring click windows..."; break; }
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
    if (!macro.ok() || macro.inputs.empty()) {
        status = "Load a .gdr macro first (inputs empty)";
        log::warn("XFC analyze rejected: no macro");
        return;
    }
    auto pl = PlayLayer::get();
    if (!pl) {
        if (!enterLevel(1)) playAfter = false;
        return;
    }
    if (phase == Phase::Recording || phase == Phase::Scanning) return;
    { std::string why; if (!levelMatches(why)) { status = why; playAfter = false; return; } }
    stopPlayback();
    auto d = Driver::create(pl); pl->addChild(d);
    phase = Phase::Recording; progress = 0; status = "Starting analysis...";
    if (resumeIfPaused(pl)) {
        // analysis must run on a live level: close the pause menu (resume) first, then start a moment later
        starting = true;
        d->schedule(schedule_selector(Driver::startLater), 0.35f);
    } else d->begin();
}

} // namespace xfc
