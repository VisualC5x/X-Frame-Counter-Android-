// Geode glue for X FRAME COUNTER (GD 2.2074, Geode 4.x).
// All analysis logic lives in engine.cpp / analysis.cpp and is unit-tested without Geode.
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.hpp"
#include "gdr.hpp"
#include "window_math.hpp"

using namespace geode::prelude;

namespace {

// ---- shared state ---------------------------------------------------------------------------
int g_step = 0;                                   // physics steps since the last reset/checkpoint
bool g_dead = false;
bool g_done = false;
bool g_playback = false;                          // replay the macro and draw rings
bool g_pending = false;                           // ANALYZE requested from the pause menu
bool g_finishQueued = false;
const std::vector<xfc::Input>* g_inj = nullptr;   // inputs injected by frame
size_t g_injPos = 0;
std::unordered_map<CheckpointObject*, int> g_cpStep;
xfc::Engine g_engine;
xfc::Macro g_macro;
std::string g_lastText;
std::string g_hudCache;


const ccColor3B kTierColors[xfc::kTierCount] = {
    {0xFF, 0x2D, 0x3C}, {0xFF, 0x8C, 0x1E}, {0xFF, 0xE1, 0x28}, {0x50, 0xE6, 0x5A},
    {0x28, 0xDC, 0xFF}, {0x32, 0x82, 0xFF}, {0xAA, 0x5A, 0xFF}};

void resyncInj() {
    g_injPos = 0;
    if (g_inj)
        while (g_injPos < g_inj->size() && (*g_inj)[g_injPos].frame < g_step) ++g_injPos;
}

bool simulating() { return g_engine.state() == xfc::Engine::State::Running; }

std::string windowText(xfc::ClickResult const& r, bool withMs) {
    if (r.dies) return "X";
    std::string t = std::to_string(r.count) + "f";
    if (r.capped) t += "+";
    if (r.earlyLimited || r.lateLimited) t += "*";
    if (r.unstable) t += "~";
    t += " (-" + std::to_string(r.earlier) + "/+" + std::to_string(r.later) + ")";
    if (withMs) t += "  " + xfc::formatMs(r.count);
    return t;
}

// ---- ring around every click --------------------------------------------------------------
class RingNode : public CCNode {
    float m_life = 0.f;
    float m_total = 1.5f;
public:
    static RingNode* create(CCPoint pos, ccColor3B col, std::string const& text, float scale, float life) {
        auto r = new RingNode();
        if (r && r->init()) {
            r->autorelease();
            r->setup(pos, col, text, scale, life);
            return r;
        }
        delete r;
        return nullptr;
    }
    void setup(CCPoint pos, ccColor3B col, std::string const& text, float scale, float life) {
        m_total = life;
        this->setPosition(pos);
        const float radius = 30.f * scale;
        const int segs = 72;
        // hollow ring made of fully opaque quads (no border fringe, no alpha anywhere)
        auto band = [&](CCDrawNode* d, float r0, float r1, ccColor4F c) {
            for (int k = 0; k < segs; ++k) {
                float a0 = 6.2831853f * static_cast<float>(k) / static_cast<float>(segs);
                float a1 = 6.2831853f * static_cast<float>(k + 1) / static_cast<float>(segs);
                CCPoint q[4] = {ccp(std::cos(a0) * r1, std::sin(a0) * r1), ccp(std::cos(a1) * r1, std::sin(a1) * r1),
                                ccp(std::cos(a1) * r0, std::sin(a1) * r0), ccp(std::cos(a0) * r0, std::sin(a0) * r0)};
                d->drawPolygon(q, 4, c, 0.f, ccColor4F{0.f, 0.f, 0.f, 0.f});
            }
        };
        auto d = CCDrawNode::create();
        band(d, radius - 9.f * scale, radius + 9.f * scale, ccColor4F{0.f, 0.f, 0.f, 1.f});        // black outline
        band(d, radius - 6.f * scale, radius + 6.f * scale, ccColor4F{col.r / 255.f, col.g / 255.f, col.b / 255.f, 1.f});
        this->addChild(d);
        if (!text.empty()) {
            const float ls = 0.62f * scale;
            const CCPoint at = ccp(0.f, radius + 20.f * scale);
            const float o = 1.6f * scale;
            const CCPoint sh[4] = {ccp(o, 0.f), ccp(-o, 0.f), ccp(0.f, o), ccp(0.f, -o)};
            for (auto const& d : sh) {
                auto b = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
                b->setScale(ls);
                b->setColor(ccc3(0, 0, 0));
                b->setPosition(ccp(at.x + d.x, at.y + d.y));
                this->addChild(b);
            }
            auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            label->setScale(ls);
            label->setColor(col);
            label->setPosition(at);
            this->addChild(label);
        }
        this->scheduleUpdate();
    }
    void update(float dt) override {
        m_life += dt;
        if (m_life >= m_total) {
            this->unscheduleUpdate();
            this->removeFromParentAndCleanup(true);
        }
    }
};

void spawnRing(GJBaseGameLayer* layer, size_t inputIndex, bool p2) {
    int ri = g_engine.resultOfInput(inputIndex);
    if (ri < 0 || ri >= static_cast<int>(g_engine.results().size())) return;
    auto const& r = g_engine.results()[static_cast<size_t>(ri)];
    g_lastText = windowText(r, Mod::get()->getSettingValue<bool>("show-ms"));
    if (!Mod::get()->getSettingValue<bool>("show-rings")) return;
    PlayerObject* pl = (p2 && layer->m_player2) ? layer->m_player2 : layer->m_player1;
    if (!pl || !layer->m_objectLayer) return;
    int tier = r.dies ? 0 : xfc::tierIndex(static_cast<double>(r.count));
    float scale = static_cast<float>(Mod::get()->getSettingValue<double>("ring-scale"));
    float life = static_cast<float>(Mod::get()->getSettingValue<double>("ring-time"));
    if (life < 0.2f) life = 0.2f;
    auto ring = RingNode::create(pl->getPosition(), kTierColors[tier],
        Mod::get()->getSettingValue<bool>("ring-label") ? windowText(r, Mod::get()->getSettingValue<bool>("show-ms")) : std::string(),
        scale, life);
    if (ring) layer->m_objectLayer->addChild(ring, 1000);
}

void updateHud() {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_uiLayer) return;
    auto hud = typeinfo_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByID("hud"_spr));
    if (!hud) return;
    std::string t;
    if (Mod::get()->getSettingValue<bool>("show-hud")) {
        if (Mod::get()->getSettingValue<bool>("hud-frame")) t = "F " + std::to_string(g_step);
        if (simulating()) {
            size_t tot = g_engine.clicksTotal();
            size_t d = g_engine.clicksDone();
            t += "\nXFC analysing " + std::to_string(tot ? d * 100 / tot : 0) + "%  (" + std::to_string(d) + "/" + std::to_string(tot) + ")";
        } else if (g_engine.state() == xfc::Engine::State::Error) {
            t += "\nXFC: " + g_engine.error();
        } else if (g_engine.state() == xfc::Engine::State::Done) {
            t += "\n" + std::to_string(g_engine.clicksTotal()) + " clicks";
            if (Mod::get()->getSettingValue<bool>("hud-last") && !g_lastText.empty()) t += "  last " + g_lastText;
            t += "\n";
            for (int k = 0; k < xfc::kTierCount; ++k) t += "T" + std::to_string(k + 1) + ":" + std::to_string(g_engine.tierCount(k)) + " ";
            if (g_engine.driftCount() > 0) t += "\n!! checkpoint drift x" + std::to_string(g_engine.driftCount());
            if (g_engine.unstableCount() > 0) t += "\n~ unstable clicks: " + std::to_string(g_engine.unstableCount());
        }
    }
    if (t == g_hudCache) return;   // only touch the label when the text really changed (no flicker)
    g_hudCache = t;
    hud->setString(t.c_str());
}

void fail(char const* msg) {
    log::warn("XFC: {}", msg);
    FLAlertLayer::create("X FRAME COUNTER", msg, "OK")->show();
}

// ---- game adapter for the engine ----------------------------------------------------------
struct Adapter : xfc::IGame {
    PlayLayer* pl = nullptr;
    std::function<void()> stepFn;

    void reset() override { pl->resetLevel(); g_step = 0; g_dead = g_done = false; resyncInj(); }
    void stepOnce() override { stepFn(); }
    int frame() const override { return g_step; }
    void* checkpoint() override {
        CheckpointObject* cp = pl->createCheckpoint();
        cp->retain();
        g_cpStep[cp] = g_step;
        return cp;
    }
    void restore(void* p) override {
        auto cp = static_cast<CheckpointObject*>(p);
        pl->loadFromCheckpoint(cp);
        auto it = g_cpStep.find(cp);
        if (it != g_cpStep.end()) g_step = it->second;
        resyncInj();
    }
    void release(void* p) override {
        auto cp = static_cast<CheckpointObject*>(p);
        g_cpStep.erase(cp);
        cp->release();
    }
    bool dead() const override { return g_dead; }
    bool done() const override { return g_done; }
    void clearFlags() override { g_dead = g_done = false; }
    void setInputs(std::vector<xfc::Input> const* v) override { g_inj = v; resyncInj(); }
    uint64_t signature() const override {
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](float v) {
            uint32_t b = 0;
            std::memcpy(&b, &v, sizeof b);
            h = (h ^ b) * 1099511628211ULL;
        };
        if (!pl) return h;
        for (PlayerObject* p : {pl->m_player1, pl->m_player2}) {
            if (!p) continue;
            mix(p->getPositionX());
            mix(p->getPositionY());
            mix(p->getRotation());
        }
        return h;
    }
};
Adapter g_adapter;

bool loadMacro(PlayLayer* pl, std::string& err) {
    auto path = Mod::get()->getSettingValue<std::filesystem::path>("macro");
    if (path.empty()) { err = "Pick a .gdr macro in the mod settings first."; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "Cannot open the macro file."; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!xfc::parseGdr(data, g_macro, err)) return false;
    if (std::fabs(g_macro.fps - 240.0) > 0.5) { err = "Macro must be 240 fps."; return false; }
    int lvl = static_cast<int>(pl->m_level->m_levelID.value());
    if (g_macro.levelId > 0 && lvl > 0 && g_macro.levelId != lvl) {
        err = "This macro was recorded for another level.";
        return false;
    }
    return true;
}

void startAnalysis(PlayLayer* pl, std::function<void()> stepFn) {
    std::string err;
    g_playback = false;
    g_lastText.clear();
    if (!loadMacro(pl, err)) { fail(err.c_str()); return; }
    g_adapter.pl = pl;
    g_adapter.stepFn = std::move(stepFn);
    xfc::EngineConfig cfg;
    cfg.maxShift = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-shift"));
    cfg.horizon = static_cast<int>(Mod::get()->getSettingValue<int64_t>("horizon"));
    cfg.pairRelease = Mod::get()->getSettingValue<bool>("pair-release");
    cfg.verify = Mod::get()->getSettingValue<bool>("verify-edges");
    cfg.lookAhead = static_cast<int>(Mod::get()->getSettingValue<int64_t>("look-ahead"));
    cfg.spanCap = static_cast<int>(Mod::get()->getSettingValue<int64_t>("span-cap"));
    if (!g_engine.start(&g_adapter, g_macro, cfg, err)) fail(err.c_str());
}

void finishAnalysis(PlayLayer* pl) {
    if (g_engine.state() == xfc::Engine::State::Error) {
        fail(g_engine.error().c_str());
        return;
    }
    g_playback = true;
    g_inj = &g_engine.inputs();
    pl->resetLevel();   // hook resets the step counter and the input cursor
}

} // namespace

// ---- hooks ------------------------------------------------------------------------------------
class $modify(XfcBase, GJBaseGameLayer) {
    void processCommands(float dt) {
        if (g_inj) {
            while (g_injPos < g_inj->size() && (*g_inj)[g_injPos].frame <= g_step) {
                size_t idx = g_injPos++;
                auto const& in = (*g_inj)[idx];
                if (in.frame != g_step) continue;
                this->handleButton(in.down, in.button, !in.p2);
                if (in.down && g_playback && !simulating()) spawnRing(this, idx, in.p2);
            }
        }
        GJBaseGameLayer::processCommands(dt);
        ++g_step;
    }

    void update(float dt) {
        PlayLayer* pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this) {
            if (g_pending) {
                g_pending = false;
                startAnalysis(pl, [this] { GJBaseGameLayer::update(1.f / 240.f); });
            }
            if (simulating()) {
                g_adapter.stepFn = [this] { GJBaseGameLayer::update(1.f / 240.f); };
                g_engine.work(0.012);
                updateHud();
                if (!simulating() && !g_finishQueued) {
                    // never reset the level from inside the update loop (that crashed): do it on the next main-thread tick
                    g_finishQueued = true;
                    Loader::get()->queueInMainThread([] {
                        g_finishQueued = false;
                        auto p = PlayLayer::get();
                        if (p && p->m_uiLayer) finishAnalysis(p);
                    });
                }
                return;   // the real-time update is skipped while analysing
            }
        }
        GJBaseGameLayer::update(dt);
    }
};

class $modify(XfcPlay, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        g_step = 0;
        g_dead = g_done = false;
        g_playback = false;
        g_pending = false;
        g_inj = nullptr;
        g_cpStep.clear();
        g_lastText.clear();
        g_finishQueued = false;
        g_hudCache = "\x01";   // force the first refresh
        if (m_uiLayer) {
            auto hud = CCLabelBMFont::create("", "chatFont.fnt");
            hud->setID("hud"_spr);
            hud->setAnchorPoint(ccp(0.f, 1.f));
            hud->setAlignment(kCCTextAlignmentLeft);
            auto win = CCDirector::get()->getWinSize();
            float s = static_cast<float>(Mod::get()->getSettingValue<double>("hud-scale"));
            hud->setScale(0.5f * s);
            hud->setPosition(ccp(6.f, win.height - 6.f));
            m_uiLayer->addChild(hud, 100);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g_step = 0;
        g_dead = g_done = false;
        resyncInj();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!simulating()) updateHud();
    }

    CheckpointObject* createCheckpoint() {
        CheckpointObject* cp = PlayLayer::createCheckpoint();
        if (cp) g_cpStep[cp] = g_step;
        return cp;
    }

    void loadFromCheckpoint(CheckpointObject* cp) {
        PlayLayer::loadFromCheckpoint(cp);
        auto it = g_cpStep.find(cp);
        if (it != g_cpStep.end()) g_step = it->second;
        resyncInj();
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (simulating()) { g_dead = true; return; }   // analysis: only record the death
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        if (simulating()) { g_done = true; return; }
        g_pending = false;   // never start an analysis while the level end screen is coming up
        PlayLayer::levelComplete();
    }

    void onQuit() {
        g_engine.shutdown();
        g_hudCache.clear();
        g_playback = false;
        g_pending = false;
        g_inj = nullptr;
        g_cpStep.clear();
        g_finishQueued = false;
        g_adapter.pl = nullptr;
        g_adapter.stepFn = nullptr;
        PlayLayer::onQuit();
    }
};

// ---- menu popup: close button top-left inside the panel, bars can never leave their track ----
class XfcPopup : public geode::Popup<PauseLayer*> {
protected:
    static constexpr int kToggles = 6;
    PauseLayer* m_pause = nullptr;

    bool setup(PauseLayer* pause) override {
        m_pause = pause;
        this->setTitle("X FRAME COUNTER");

        // close (X) button: pulled fully inside the panel's top-left corner, always on top
        if (m_closeBtn) {
            m_closeBtn->setPosition(ccp(24.f, m_size.height - 24.f));
            m_closeBtn->setZOrder(50);
        }

        // big round icon
        if (auto icon = CCSprite::create(("xfc-circle.png"_spr).c_str())) {
            icon->setScale(140.f / std::max(1.f, std::max(icon->getContentSize().width, icon->getContentSize().height)));
            m_mainLayer->addChildAtPosition(icon, Anchor::Center, ccp(-150.f, 34.f));
        }

        // tier bars, drawn in one node centred on the panel
        static const char* names[xfc::kTierCount] = {"1", "2", "3", "4-5", "6-8", "9-15", "16+"};
        int counts[xfc::kTierCount];
        int maxCount = 1;
        for (int k = 0; k < xfc::kTierCount; ++k) {
            counts[k] = g_engine.state() == xfc::Engine::State::Done ? g_engine.tierCount(k) : 0;
            maxCount = std::max(maxCount, counts[k]);
        }
        auto bars = CCDrawNode::create();
        const float trackX = -30.f, trackW = 150.f, rowH = 7.f;
        for (int k = 0; k < xfc::kTierCount; ++k) {
            float y = 78.f - 17.f * static_cast<float>(k);
            CCPoint track[4] = {ccp(trackX, y), ccp(trackX + trackW, y), ccp(trackX + trackW, y + rowH), ccp(trackX, y + rowH)};
            bars->drawPolygon(track, 4, ccColor4F{0.12f, 0.16f, 0.28f, 1.f}, 0.f, ccColor4F{0.f, 0.f, 0.f, 0.f});
            float w = trackW * static_cast<float>(counts[k]) / static_cast<float>(maxCount);
            if (counts[k] > 0 && w < 3.f) w = 3.f;
            if (w > trackW) w = trackW;
            if (w > 0.f) {
                CCPoint fill[4] = {ccp(trackX, y), ccp(trackX + w, y), ccp(trackX + w, y + rowH), ccp(trackX, y + rowH)};
                auto c = kTierColors[k];
                bars->drawPolygon(fill, 4, ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f}, 0.f, ccColor4F{0.f, 0.f, 0.f, 0.f});
            }
            auto name = CCLabelBMFont::create(names[k], "bigFont.fnt");
            name->setScale(0.35f);
            name->setColor(kTierColors[k]);
            m_mainLayer->addChildAtPosition(name, Anchor::Center, ccp(trackX - 32.f, y + rowH / 2.f));
            auto num = CCLabelBMFont::create(std::to_string(counts[k]).c_str(), "bigFont.fnt");
            num->setScale(0.35f);
            num->setColor(kTierColors[k]);
            m_mainLayer->addChildAtPosition(num, Anchor::Center, ccp(trackX + trackW + 26.f, y + rowH / 2.f));
        }
        m_mainLayer->addChildAtPosition(bars, Anchor::Center, ccp(0.f, 0.f));

        std::string sum = "Run ANALYZE to measure the macro";
        if (g_engine.state() == xfc::Engine::State::Done) {
            int tight = 0;
            bool any = false;
            for (auto const& r : g_engine.results())
                if (!r.dies && (!any || r.count < tight)) { tight = r.count; any = true; }
            sum = std::to_string(g_engine.clicksTotal()) + " clicks   tightest window " + (any ? std::to_string(tight) : "-") + " f";
            if (g_engine.driftCount() > 0) sum += "   drift x" + std::to_string(g_engine.driftCount());
            if (g_engine.unstableCount() > 0) sum += "   ~" + std::to_string(g_engine.unstableCount());
        } else if (g_engine.state() == xfc::Engine::State::Error) {
            sum = g_engine.error();
        }
        auto sumLabel = CCLabelBMFont::create(sum.c_str(), "chatFont.fnt");
        sumLabel->setScale(0.6f);
        m_mainLayer->addChildAtPosition(sumLabel, Anchor::Center, ccp(30.f, -46.f));

        // toggles
        struct T { char const* key; char const* text; };
        static const T toggles[kToggles] = {{"show-rings", "Rings"}, {"ring-label", "Labels"}, {"show-ms", "ms"},
                                            {"show-hud", "HUD"}, {"hud-frame", "F count"}, {"hud-last", "Last"}};
        for (int k = 0; k < kToggles; ++k) {
            float x = -190.f + 76.f * static_cast<float>(k);
            auto tg = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(XfcPopup::onToggle), 0.6f);
            tg->setTag(k);
            tg->toggle(Mod::get()->getSettingValue<bool>(toggles[k].key));
            m_buttonMenu->addChildAtPosition(tg, Anchor::Center, ccp(x, -72.f));
            auto lab = CCLabelBMFont::create(toggles[k].text, "bigFont.fnt");
            lab->setScale(0.35f);
            m_mainLayer->addChildAtPosition(lab, Anchor::Center, ccp(x + 28.f, -72.f));
        }

        auto analyze = makeIconButton(("xfc-analyze.png"_spr).c_str(), "ANALYZE", this, menu_selector(XfcPopup::onAnalyze));
        m_buttonMenu->addChildAtPosition(analyze, Anchor::Center, ccp(-70.f, -118.f));
        auto settings = makeIconButton(("xfc-settings.png"_spr).c_str(), "SETTINGS", this, menu_selector(XfcPopup::onSettings));
        m_buttonMenu->addChildAtPosition(settings, Anchor::Center, ccp(70.f, -118.f));
        return true;
    }

    // round icon + caption; the whole thing is one button
    static CCNode* makeIconButtonSprite(char const* file, char const* text) {
        constexpr float W = 96.f, H = 76.f, I = 50.f;
        auto holder = CCNode::create();
        holder->setContentSize(CCSize{W, H});
        if (auto img = CCSprite::create(file)) {
            auto cs = img->getContentSize();
            img->setScale(I / std::max(1.f, std::max(cs.width, cs.height)));
            img->setPosition(ccp(W / 2.f, H - I / 2.f - 1.f));
            holder->addChild(img);
        }
        auto lab = CCLabelBMFont::create(text, "bigFont.fnt");
        lab->setScale(0.4f);
        lab->setPosition(ccp(W / 2.f, 9.f));
        holder->addChild(lab);
        return holder;
    }

    static CCMenuItemSpriteExtra* makeIconButton(char const* file, char const* text, CCObject* target, SEL_MenuHandler cb) {
        return CCMenuItemSpriteExtra::create(makeIconButtonSprite(file, text), target, cb);
    }

    void onToggle(CCObject* sender) {
        static const char* keys[kToggles] = {"show-rings", "ring-label", "show-ms", "show-hud", "hud-frame", "hud-last"};
        auto tg = static_cast<CCMenuItemToggler*>(sender);
        int k = tg->getTag();
        if (k >= 0 && k < kToggles) Mod::get()->setSettingValue<bool>(keys[k], !tg->isToggled());
    }

    void onAnalyze(CCObject*) {
        if (m_pause) m_pause->onResume(nullptr);
        g_pending = true;
        this->onClose(nullptr);
    }

    void onSettings(CCObject*) { openSettingsPopup(Mod::get()); }

public:
    static XfcPopup* create(PauseLayer* pause) {
        auto ret = new XfcPopup();
        if (ret && ret->initAnchored(460.f, 320.f, pause)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class $modify(XfcPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) return;
        constexpr float kBtn = 34.f;   // normal pause-menu button size
        CCNode* spr = nullptr;
        if (auto img = CCSprite::create(("xfc-btn.png"_spr).c_str())) {
            auto cs = img->getContentSize();
            img->setScale(kBtn / std::max(1.f, std::max(cs.width, cs.height)));
            auto holder = CCNode::create();
            holder->setContentSize(CCSize{kBtn, kBtn});
            img->setPosition(ccp(kBtn / 2.f, kBtn / 2.f));
            holder->addChild(img);
            spr = holder;
        } else {
            spr = ButtonSprite::create("XFC");
        }
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(XfcPause::onXfc));
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onXfc(CCObject*) {
        if (simulating()) { g_engine.abort(); this->onResume(nullptr); return; }
        if (auto popup = XfcPopup::create(this)) popup->show();
    }
};

$on_mod(Loaded) {
    log::info("X FRAME COUNTER loaded");
}
