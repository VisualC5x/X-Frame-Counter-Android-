// XFC menu - modern dark UI: left sidebar with tabs, scrolling content with rounded rows, live settings.
// Every page scrolls inside a clipped area, so nothing can ever stick out of the panel or hide behind something else.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iterator>
#include <map>
#include <vector>
#include "XFC.hpp"
#include <Geode/ui/GeodeUI.hpp>

namespace xfc {

class XFCPanel;
static XFCPanel* s_openPanel = nullptr;

namespace xui {
inline constexpr ccColor3B kBg{22, 20, 44}, kBorder{78, 70, 148}, kSide{31, 29, 60}, kRow{41, 39, 78}, kRowHi{62, 57, 118},
    kBtn{52, 49, 98}, kAcc{128, 108, 255}, kAcc2{176, 160, 255}, kText{238, 238, 252}, kDim{152, 150, 192},
    kGood{100, 238, 150}, kWarn{255, 210, 100}, kBad{255, 105, 115}, kOff{66, 63, 108};
inline ccColor4F c4(ccColor3B c, float a = 1.f) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

// rounded rectangle, origin bottom-left (pure geometry: no texture, looks the same on every device)
inline void roundInto(CCDrawNode* d, float w, float h, float r, ccColor3B fill, float a = 1.f, ccColor3B border = {0, 0, 0}, float bw = 0.f) {
    r = std::min(r, std::min(w, h) / 2.f);
    std::vector<CCPoint> p;
    const int seg = 6;
    auto arc = [&](float cx, float cy, float a0) {
        for (int i = 0; i <= seg; i++) { float t = a0 + (float)i / seg * 1.5707963f; p.push_back(ccp(cx + r * std::cos(t), cy + r * std::sin(t))); }
    };
    arc(w - r, h - r, 0.f); arc(r, h - r, 1.5707963f); arc(r, r, 3.1415926f); arc(w - r, r, 4.712389f);
    d->drawPolygon(p.data(), (unsigned)p.size(), c4(fill, a), bw, c4(border, bw > 0.f ? 1.f : 0.f));
}
inline CCDrawNode* rrect(float w, float h, float r, ccColor3B fill, float a = 1.f, ccColor3B border = {0, 0, 0}, float bw = 0.f) {
    auto d = CCDrawNode::create(); roundInto(d, w, h, r, fill, a, border, bw); return d;
}

// a menu that ignores taps outside of its scroll area (items scrolled out of view can never be pressed by accident)
class ClipMenu : public CCMenu {
public:
    CCNode* m_clip = nullptr;
    static ClipMenu* create(CCNode* clip) {
        auto r = new ClipMenu();
        if (r && r->init()) { r->m_clip = clip; r->autorelease(); return r; }
        delete r; return nullptr;
    }
    bool ccTouchBegan(CCTouch* t, CCEvent* e) override {
        if (m_clip) {
            auto p = m_clip->convertTouchToNodeSpace(t); auto s = m_clip->getContentSize();
            if (p.x < 0 || p.y < 0 || p.x > s.width || p.y > s.height) return false;
        }
        return CCMenu::ccTouchBegan(t, e);
    }
};
}
using namespace xui;

// Phones get bigger touch targets (rows, buttons, tabs, close) - the layout is the same, only the hit areas grow.
#ifdef GEODE_IS_MOBILE
inline constexpr float kRowH = 34.f, kBtnH = 28.f, kSq = 34.f, kTabH = 27.f, kCloseD = 30.f, kListH = 46.f;
#else
inline constexpr float kRowH = 28.f, kBtnH = 22.f, kSq = 26.f, kTabH = 25.f, kCloseD = 24.f, kListH = 36.f;
#endif

static const ccColor3B kPalette[] = {
    {255, 70, 80}, {255, 115, 115}, {255, 160, 80}, {255, 215, 90}, {170, 235, 100}, {90, 230, 130}, {80, 225, 225}, {80, 160, 255},
    {110, 125, 255}, {160, 110, 255}, {200, 120, 255}, {255, 130, 230}, {255, 255, 255}, {150, 160, 175}, {0, 255, 170}, {255, 190, 120}};
static constexpr int kPalN = sizeof(kPalette) / sizeof(kPalette[0]);

struct StepDef { double lo, hi, inc; bool flt; const char* suffix; };
static const StepDef* stepDef(const std::string& k) {
    static const std::map<std::string, StepDef> m = {
        {"hud-scale", {0.5, 2.0, 0.1, true, "x"}}, {"ring-scale", {0.4, 2.5, 0.1, true, "x"}}, {"ring-time", {0.0, 8.0, 0.5, true, "s"}},
        {"max-shift", {5, 30, 1, false, ""}}, {"horizon", {60, 6000, 60, false, ""}}, {"sound-volume", {0.0, 1.0, 0.1, true, ""}}};
    auto it = m.find(k); return it == m.end() ? nullptr : &it->second;
}

class XFCPanel : public Popup<> {
protected:
    ~XFCPanel() override { if (s_openPanel == this) s_openPanel = nullptr; }

    static constexpr float W = 540, H = 292, SW = 132;                 // GD's screen is 320 units high: the panel always fits
    static constexpr float CX = SW + 18, FOOT = 30;                    // content area: x from CX, y from FOOT
    static constexpr float CW = W - CX - 12, CH = H - 48 - FOOT;
    static constexpr int NTABS = 7;

    struct Page { ScrollLayer* sc = nullptr; CCNode* body = nullptr; ClipMenu* menu = nullptr; float y = 0, w = 0, viewH = 0; };

    CCNode* m_tabs[NTABS]{};
    CCNode* m_tabHi[NTABS]{};
    CCLabelBMFont* m_tabLbl[NTABS]{};
    CCLabelBMFont* m_title = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLayerColor* m_barFill = nullptr;
    std::array<CCLayerColor*, kTierCount> m_tierBars{};
    std::array<CCLabelBMFont*, kTierCount> m_tierNum{};
    std::array<CCDrawNode*, kTierCount> m_sw{};
    CCLabelBMFont* m_summary = nullptr;
    CCLabelBMFont* m_xdInfo = nullptr;
    std::vector<std::string> m_keys;
    std::map<std::string, CCLabelBMFont*> m_vals;
    std::map<std::string, std::pair<const int*, int>> m_presets;
    std::vector<std::filesystem::path> m_files;
    CCNode* m_list = nullptr;
    int m_tab = 0;
    EventListener<Task<Result<std::filesystem::path>>> m_pick;

    // ---------------------------------------------------------------- small widgets
    CCLabelBMFont* txt(const std::string& t, float scale, ccColor3B c, CCPoint pos, CCNode* parent, CCPoint anchor = {0.f, 0.5f}, float maxW = 0.f) {
        auto l = CCLabelBMFont::create(t.c_str(), font()); l->setScale(scale); l->setColor(c); l->setPosition(pos); l->setAnchorPoint(anchor);
        if (maxW > 0.f) l->limitLabelWidth(maxW, scale, 0.1f);
        parent->addChild(l); return l;
    }
    CCMenuItemSpriteExtra* pillBtn(CCNode* menu, const std::string& t, ccColor3B col, CCPoint at, float w, SEL_MenuHandler cb, int tag = 0, float h = kBtnH) {
        auto n = CCNode::create(); n->setContentSize({w, h});
        n->addChild(rrect(w, h, 8, kBtn, 1.f, col, 1.2f));
        txt(t, 0.34f, col, {w / 2, h / 2 + 0.5f}, n, {0.5f, 0.5f}, w - 10.f);
        auto b = CCMenuItemSpriteExtra::create(n, nullptr, this, cb); b->setPosition(at); b->setTag(tag); menu->addChild(b);
        return b;
    }
    CCNode* pill(bool on) {
        auto n = CCNode::create(); n->setContentSize({40, 20});
        n->addChild(rrect(40, 20, 10, on ? kAcc : kOff));
        auto k = CCDrawNode::create(); k->drawDot(ccp(on ? 30.f : 10.f, 10.f), 7.f, c4({255, 255, 255})); n->addChild(k);
        return n;
    }

    // ---------------------------------------------------------------- pages
    Page makePage(CCNode* parent, float reserveTop = 0.f) {
        Page p; p.w = CW; p.viewH = CH - reserveTop;
        p.sc = ScrollLayer::create({CW, p.viewH}); p.sc->setPosition(CX, FOOT); parent->addChild(p.sc);
        p.body = CCNode::create(); p.sc->m_contentLayer->addChild(p.body);
        p.menu = ClipMenu::create(p.sc); p.menu->setPosition(0, 0); p.body->addChild(p.menu, 5);
        return p;
    }
    void endPage(Page& p) {
        float h = std::max(p.viewH, -p.y + 6.f);
        p.sc->m_contentLayer->setContentSize({CW, h});
        p.body->setPosition({0, h});
        p.sc->moveToTop();
    }
    void section(Page& p, const std::string& t) {
        txt(t, 0.32f, kAcc2, {4, p.y - 9}, p.body, {0.f, 0.5f}, p.w - 8);
        p.y -= 22;
    }
    void note(Page& p, const std::string& t, ccColor3B c = kDim) {
        txt(t, 0.28f, c, {6, p.y - 7}, p.body, {0.f, 0.5f}, p.w - 12);
        p.y -= 15;
    }
    float beginRow(Page& p, const std::string& name, float h = kRowH, ccColor3B nameCol = kText) {
        float cy = p.y - h / 2;
        auto bg = rrect(p.w, h, 9, kRow); bg->setPosition({0, p.y - h}); p.body->addChild(bg);
        if (!name.empty()) txt(name, 0.38f, nameCol, {12, cy}, p.body, {0.f, 0.5f}, p.w - 140.f);
        p.y -= h + 5;
        return cy;
    }
    void addToggle(Page& p, const char* key, const char* name) {
        float cy = beginRow(p, name);
        auto t = CCMenuItemToggler::create(pill(false), pill(true), this, menu_selector(XFCPanel::onToggle));
        t->toggle(optB(key)); t->setPosition({p.w - 12 - 20, cy}); t->setTag((int)m_keys.size()); m_keys.push_back(key); p.menu->addChild(t);
    }
    // [-] value [+]: the value label is centred in a fixed slot, so changing digits never moves anything
    void addStep(Page& p, const char* key, const char* name) {
        float cy = beginRow(p, name); int id = (int)m_keys.size(); m_keys.push_back(key);
        pillBtn(p.menu, "-", kAcc2, {p.w - 120, cy}, kSq, menu_selector(XFCPanel::onStepDown), id);
        pillBtn(p.menu, "+", kAcc2, {p.w - 24, cy}, kSq, menu_selector(XFCPanel::onStepUp), id);
        m_vals[key] = txt("", 0.4f, kText, {p.w - 72, cy}, p.body, {0.5f, 0.5f}); refreshVal(key);
    }
    void addPreset(Page& p, const char* key, const char* name, const int* arr, int n) {
        float cy = beginRow(p, name); int id = (int)m_keys.size(); m_keys.push_back(key); m_presets[key] = {arr, n};
        pillBtn(p.menu, "-", kAcc2, {p.w - 120, cy}, kSq, menu_selector(XFCPanel::onPresetDown), id);
        pillBtn(p.menu, "+", kAcc2, {p.w - 24, cy}, kSq, menu_selector(XFCPanel::onPresetUp), id);
        m_vals[key] = txt("", 0.4f, kText, {p.w - 72, cy}, p.body, {0.5f, 0.5f}); refreshVal(key);
    }
    void addButtonRow(Page& p, const char* name, const char* btn, ccColor3B col, SEL_MenuHandler cb) {
        float cy = beginRow(p, name);
        pillBtn(p.menu, btn, col, {p.w - 12 - 62, cy}, 124, cb);
    }
    void refreshVal(const std::string& key) {
        auto it = m_vals.find(key); if (it == m_vals.end()) return;
        if (auto d = stepDef(key)) {
            it->second->setString((d->flt ? fmt::format("{:.1f}{}", optD(key), d->suffix) : fmt::format("{}{}", optI(key), d->suffix)).c_str());
        } else it->second->setString(std::to_string(optI(key)).c_str());
        it->second->limitLabelWidth(44.f, 0.4f, 0.1f);
    }
    void hudRefresh() { if (auto h = State::get().hud) { h->setVisible(optB("show-hud")); h->relayout(); } }

    // ---------------------------------------------------------------- setup
    bool setup() override {
        auto sz = m_mainLayer->getContentSize();
        m_bgSprite->setVisible(false);
        if (m_closeBtn) { m_closeBtn->setVisible(false); m_closeBtn->setEnabled(false); }

        auto shadow = rrect(W + 8, H + 8, 20, {0, 0, 0}, 0.45f); shadow->setPosition({-4, -4}); m_mainLayer->addChild(shadow, -2);
        auto panel = rrect(W, H, 18, kBg, 1.f, kBorder, 1.6f); m_mainLayer->addChild(panel, -1);
        auto side = rrect(SW, H - 16, 14, kSide); side->setPosition({8, 8}); m_mainLayer->addChild(side, 0);

        // header
        if (auto ic = CCSprite::create("xfc-circle.png"_spr)) {
            ic->setScale(34.f / std::max(1.f, std::max(ic->getContentWidth(), ic->getContentHeight())));
            ic->setPosition({8 + 28.f, H - 34.f}); m_mainLayer->addChild(ic, 2);
        }
        txt("XFC", 0.52f, kText, {8 + 52.f, H - 29.f}, m_mainLayer, {0.f, 0.5f}, 70.f);
        txt("Frame Counter", 0.26f, kDim, {8 + 52.f, H - 44.f}, m_mainLayer, {0.f, 0.5f}, 74.f);
        txt(fmt::format("by LNTB017  {}", Mod::get()->getVersion().toVString()), 0.23f, kDim, {8 + SW / 2, 20.f}, m_mainLayer, {0.5f, 0.5f}, SW - 16.f);

        // tabs
        const char* names[NTABS] = {"Macros", "Analysis", "Visuals", "Colours", "Speed", "xdBot", "Settings"};
        auto tm = CCMenu::create(); tm->setPosition(0, 0); m_mainLayer->addChild(tm, 3);
        for (int i = 0; i < NTABS; i++) {
            auto n = CCNode::create(); n->setContentSize({SW - 20, kTabH});
            m_tabHi[i] = rrect(SW - 20, kTabH, 9, kRowHi); n->addChild(m_tabHi[i]); m_tabHi[i]->setVisible(false);
            m_tabLbl[i] = txt(names[i], 0.4f, kDim, {14, kTabH / 2}, n, {0.f, 0.5f}, SW - 44.f);
            auto b = CCMenuItemSpriteExtra::create(n, nullptr, this, menu_selector(XFCPanel::onTab));
            b->setTag(i); b->setPosition({8 + SW / 2, H - 80.f - i * 28.f}); tm->addChild(b);
        }

        // title row + close
        m_title = txt("", 0.62f, kText, {CX, H - 26.f}, m_mainLayer, {0.f, 0.5f}, CW - 40.f);
        auto top = CCMenu::create(); top->setPosition(0, 0); m_mainLayer->addChild(top, 30);
        {
            auto n = CCNode::create(); n->setContentSize({kCloseD, kCloseD});
            n->addChild(rrect(kCloseD, kCloseD, kCloseD / 2, kBtn, 1.f, kBorder, 1.2f));
            txt("x", 0.5f, kText, {kCloseD / 2, kCloseD / 2 + 0.5f}, n, {0.5f, 0.5f});
            auto b = CCMenuItemSpriteExtra::create(n, nullptr, this, menu_selector(XFCPanel::onCloseBtn));
            b->setPosition({W - 14.f - kCloseD / 2, H - 14.f - kCloseD / 2}); top->addChild(b);
        }

        // footer: status + progress
        m_status = txt(State::get().status, 0.28f, kDim, {CX, 20.f}, m_mainLayer, {0.f, 0.5f}, CW);
        auto track = rrect(CW, 4, 2, kRow); track->setPosition({CX, 8}); m_mainLayer->addChild(track, 1);
        m_barFill = CCLayerColor::create({kAcc.r, kAcc.g, kAcc.b, 255}, 1, 4); m_barFill->setPosition({CX, 8}); m_barFill->setScaleX(0.f); m_mainLayer->addChild(m_barFill, 2);

        for (int i = 0; i < NTABS; i++) { m_tabs[i] = CCNode::create(); m_tabs[i]->setContentSize(sz); m_mainLayer->addChild(m_tabs[i], 1); }
        buildMacros(); buildAnalysis(); buildVisuals(); buildColors(); buildSpeed(); buildXdbot(); buildSettings();
        showTab(0, false);
        this->schedule(schedule_selector(XFCPanel::tick), 0.05f);
        return true;
    }

    // ---- MACROS
    void buildMacros() {
        auto menu = CCMenu::create(); menu->setPosition(0, 0); m_tabs[0]->addChild(menu, 3);
        float y = FOOT + CH - 13.f, R = CX + CW;
        pillBtn(menu, "UPLOAD", kAcc2, {CX + 34, y}, 68, menu_selector(XFCPanel::onUpload));
        pillBtn(menu, "REFRESH", kAcc2, {CX + 106, y}, 68, menu_selector(XFCPanel::onRefresh));
        pillBtn(menu, "PLAY", kGood, {R - 153, y}, 50, menu_selector(XFCPanel::onPlay));
        pillBtn(menu, "STOP", kBad, {R - 99, y}, 50, menu_selector(XFCPanel::onStop));
        pillBtn(menu, "ANALYZE", kWarn, {R - 35, y}, 70, menu_selector(XFCPanel::onAnalyze));
        m_list = CCNode::create(); m_tabs[0]->addChild(m_list);
        fillList();
    }
    void fillList() {
        m_list->removeAllChildren(); m_files = State::get().findMacros();
        auto p = makePage(m_list, 32.f);
        if (m_files.empty()) note(p, "No .gdr macros yet - press UPLOAD or put them in the xdBot macros folder");
        auto xdDir = State::get().xdbotFolder();
        for (size_t i = 0; i < m_files.size(); i++) {
            bool cur = m_files[i].string() == State::get().macroPath;
            float h = kListH, cy = p.y - h / 2;
            auto bg = rrect(p.w, h, 10, cur ? kRowHi : kRow, 1.f, cur ? kAcc : kRow, cur ? 1.2f : 0.f); bg->setPosition({0, p.y - h}); p.body->addChild(bg);
            txt(m_files[i].filename().string(), 0.36f, cur ? kText : ccColor3B{208, 208, 232}, {12, cy + 6}, p.body, {0.f, 0.5f}, p.w - 196.f);
            bool fromXd = !xdDir.empty() && m_files[i].parent_path() == xdDir;
            txt(fromXd ? "xdBot folder" : "XFC folder", 0.23f, fromXd ? kWarn : kDim, {12, cy - 9}, p.body, {0.f, 0.5f});
            pillBtn(p.menu, "LOAD", cur ? kGood : kAcc2, {p.w - 8 - 44 - 4 - 62 - 4 - 23, cy}, 46, menu_selector(XFCPanel::onLoad), (int)i);
            pillBtn(p.menu, "ANALYZE", kWarn, {p.w - 8 - 44 - 4 - 31, cy}, 62, menu_selector(XFCPanel::onRowAnalyze), (int)i);
            pillBtn(p.menu, "PLAY", kGood, {p.w - 8 - 22, cy}, 44, menu_selector(XFCPanel::onRowPlay), (int)i);
            p.y -= h + 5;
        }
        endPage(p);
    }

    // ---- ANALYSIS
    void buildAnalysis() {
        auto p = makePage(m_tabs[1]);
        section(p, "CLICK WINDOWS (frames)");
        float barX = 74, barW = p.w - barX - 52;
        for (int i = (int)kTierCount - 1; i >= 0; i--) {
            float cy = beginRow(p, "", 22.f);
            auto c = tierColor(i);
            txt(std::string(kTiers[i].label), 0.36f, c, {12, cy}, p.body, {0.f, 0.5f}, 56.f);
            auto track = rrect(barW, 6, 3, kBg); track->setPosition({barX, cy - 3}); p.body->addChild(track);
            m_tierBars[i] = CCLayerColor::create({c.r, c.g, c.b, 255}, 1, 6); m_tierBars[i]->setPosition({barX, cy - 3}); m_tierBars[i]->setScaleX(0.f); p.body->addChild(m_tierBars[i]);
            m_tierNum[i] = txt("0", 0.36f, c, {p.w - 12, cy}, p.body, {1.f, 0.5f});
        }
        p.y -= 4;
        m_summary = txt("", 0.3f, kDim, {6, p.y - 8}, p.body, {0.f, 0.5f}, p.w - 12); p.y -= 20;
        endPage(p);
    }

    // ---- VISUALS
    void buildVisuals() {
        auto p = makePage(m_tabs[2]);
        section(p, "COUNTER");
        addToggle(p, "show-hud", "Show counter");
        addToggle(p, "show-tiers", "Window rows 0-1 ... 15-20");
        addToggle(p, "show-ms", "Window in ms");
        addToggle(p, "show-frame", "Frame counter");
        addToggle(p, "keep-last-window", "Keep last click window");
        addStep(p, "hud-scale", "Counter size");
        note(p, "The counter sits in the top-left corner of the play screen");
        section(p, "CLICK CIRCLES");
        addToggle(p, "show-rings", "Circle on every click");
        addToggle(p, "show-release-rings", "Circle on every release");
        addToggle(p, "ring-label", "Exact frame on circle");
        addStep(p, "ring-scale", "Circle size");
        addStep(p, "ring-time", "Circle lifetime (0 = stay)");
        endPage(p);
    }

    // ---- COLOURS
    void paintSw(int i) { if (m_sw[i]) { m_sw[i]->clear(); roundInto(m_sw[i], 40, 16, 6, tierColor(i), 1.f, {255, 255, 255}, 1.f); } }
    void buildColors() {
        auto p = makePage(m_tabs[3]);
        section(p, "ROW COLOURS");
        for (int i = (int)kTierCount - 1; i >= 0; i--) {
            float cy = beginRow(p, std::string(kTiers[i].label) + " frames", kRowH - 2.f);
            m_sw[i] = CCDrawNode::create(); m_sw[i]->setPosition({p.w - 138, cy - 8}); p.body->addChild(m_sw[i]); paintSw(i);
            pillBtn(p.menu, "<", kAcc2, {p.w - 62, cy}, kSq, menu_selector(XFCPanel::onColor), i * 2);
            pillBtn(p.menu, ">", kAcc2, {p.w - 24, cy}, kSq, menu_selector(XFCPanel::onColor), i * 2 + 1);
        }
        addButtonRow(p, "Back to default colours", "RESET", kWarn, menu_selector(XFCPanel::onColorReset));
        note(p, "Exact RGB: Geode > XFC > Settings");
        endPage(p);
    }

    // ---- SPEED
    void buildSpeed() {
        auto p = makePage(m_tabs[4]);
        section(p, "PHYSICS (TPS)");
        addToggle(p, "tps-enabled", "Custom TPS");
        addPreset(p, "tps", "TPS", kTpsPresets, (int)std::size(kTpsPresets));
        section(p, "VIDEO (FPS)");
        addToggle(p, "fps-enabled", "Custom FPS");
        addPreset(p, "fps", "FPS", kFpsPresets, (int)std::size(kFpsPresets));
        note(p, "Macros are recorded at 240 TPS - keep TPS at 240 when playing them.", kWarn);
        note(p, "Analysis always runs at 240 TPS internally, whatever you set here.");
        endPage(p);
    }
    void preset(CCObject* o, int dir) {
        auto key = m_keys[static_cast<CCNode*>(o)->getTag()]; auto [arr, n] = m_presets[key];
        int64_t cur = optI(key); int idx = 0;
        for (int i = 0; i < n; i++) if (arr[i] <= cur) idx = i;
        idx = std::clamp(idx + dir, 0, n - 1);
        if (Mod::get()->hasSetting(key)) Mod::get()->setSettingValue<int64_t>(key, arr[idx]);
        refreshVal(key);
        if (key == "fps") applyFps();
    }
    void onPresetDown(CCObject* o) { preset(o, -1); }
    void onPresetUp(CCObject* o) { preset(o, +1); }

    // ---- XDBOT
    void buildXdbot() {
        auto p = makePage(m_tabs[5]);
        section(p, "XDBOT LINK");
        float cy = beginRow(p, "", kRowH); m_xdInfo = txt("", 0.32f, kText, {12, cy}, p.body, {0.f, 0.5f}, p.w - 24);
        addToggle(p, "link-xdbot", "Sync with xdBot playback");
        addButtonRow(p, "Copy macro into xdBot folder", "SEND", kAcc2, menu_selector(XFCPanel::onSend));
        note(p, "1. Load a .gdr in XFC and ANALYZE it once.");
        note(p, "2. SEND it to xdBot, then in xdBot: Load > Play.");
        note(p, "3. XFC follows the frame counter: rows, circles, frames and ms");
        note(p, "    fire on every analysed click. No xdBot changes needed.");
        endPage(p);
    }

    // ---- SETTINGS
    void buildSettings() {
        auto p = makePage(m_tabs[6]);
        section(p, "ANALYSIS");
        addStep(p, "max-shift", "Max window searched (f)");
        addStep(p, "horizon", "Survive-check length (f)");
        section(p, "SOUND");
        addToggle(p, "tier-sounds", "Sound on every click window");
        addStep(p, "sound-volume", "Volume");
        section(p, "MORE");
        addButtonRow(p, "Every setting with exact values", "GEODE", kWarn, menu_selector(XFCPanel::onGeode));
        note(p, "Works in every gamemode and dual: the real game physics are re-simulated.");
        endPage(p);
    }

    // ---------------------------------------------------------------- behaviour
    void showTab(int i, bool animate = true) {
        static const char* titles[NTABS] = {"Macros", "Analysis", "Visuals", "Colours", "Speed", "xdBot", "Settings"};
        m_tab = i; m_title->setString(titles[i]);
        for (int t = 0; t < NTABS; t++) {
            m_tabs[t]->stopAllActions(); m_tabs[t]->setVisible(t == i);
            m_tabLbl[t]->setColor(t == i ? kText : kDim); m_tabHi[t]->setVisible(t == i);
            if (t == i && animate) { m_tabs[t]->setPosition(14, 0); m_tabs[t]->runAction(CCEaseExponentialOut::create(CCMoveTo::create(0.28f, {0, 0}))); }
            else if (t == i) m_tabs[t]->setPosition(0, 0);
        }
    }
    void onTab(CCObject* o) { showTab(static_cast<CCNode*>(o)->getTag()); }
    void onCloseBtn(CCObject*) { this->onClose(nullptr); }
    void rebuildList(float) { this->unschedule(schedule_selector(XFCPanel::rebuildList)); fillList(); }
    void queueList() { this->scheduleOnce(schedule_selector(XFCPanel::rebuildList), 0.f); }   // never rebuild a menu from inside its own tap
    void onRefresh(CCObject*) { State::get().status = "Refreshed"; queueList(); }
    void onUpload(CCObject*) {
        file::FilePickOptions opts; opts.filters.push_back({"GD replay", {"*.gdr"}});
        m_pick.bind([this](auto* ev) {
            auto r = ev->getValue(); if (!r || !r->isOk()) return;
            auto src = r->unwrap(); auto& s = State::get();
            auto ext = src.extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".gdr") { s.status = "Only .gdr files are accepted"; return; }
            auto dst = s.xfcDir() / src.filename();
            std::error_code ec; std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            s.loadMacro(dst); queueList();
        });
        m_pick.setFilter(file::pick(file::PickMode::OpenFile, opts));
    }
    void onLoad(CCObject* o) { auto i = static_cast<CCNode*>(o)->getTag(); if (i < (int)m_files.size()) { State::get().loadMacro(m_files[i]); queueList(); } }
    bool started() { auto& s = State::get(); return s.playing || s.simulating || s.starting; }
    void onRowAnalyze(CCObject* o) {
        auto i = static_cast<CCNode*>(o)->getTag();
        if (i < (int)m_files.size() && State::get().loadMacro(m_files[i])) {
            State::get().startAnalysis();
            if (started()) this->onClose(nullptr); else queueList();
        }
    }
    void onRowPlay(CCObject* o) {
        auto i = static_cast<CCNode*>(o)->getTag();
        if (i < (int)m_files.size() && State::get().loadMacro(m_files[i])) { State::get().analyzeAndPlay(); if (started()) this->onClose(nullptr); else queueList(); }
    }
    void onPlay(CCObject*) { State::get().analyzeAndPlay(); if (started()) this->onClose(nullptr); }
    void onStop(CCObject*) { State::get().stopPlayback(); }
    void onAnalyze(CCObject*) { State::get().startAnalysis(); if (started()) this->onClose(nullptr); }
    void onSend(CCObject*) { State::get().sendToXdbot(); }
    void onGeode(CCObject*) { geode::openSettingsPopup(Mod::get()); }

    void onToggle(CCObject* o) {
        auto t = static_cast<CCMenuItemToggler*>(o); auto key = m_keys[t->getTag()];
        bool now = !t->isToggled();                                   // isToggled() is the state before the tap
        if (Mod::get()->hasSetting(key)) Mod::get()->setSettingValue<bool>(key, now);
        if (key == "fps-enabled") { if (now) applyFps(); else CCDirector::get()->setAnimationInterval(1.0 / 60.0); }
        hudRefresh();
    }
    void step(CCObject* o, int dir) {
        auto key = m_keys[static_cast<CCNode*>(o)->getTag()]; auto mod = Mod::get(); auto d = stepDef(key);
        if (!d || !mod->hasSetting(key)) { refreshVal(key); return; }
        if (d->flt) mod->setSettingValue<double>(key, std::clamp(std::round((optD(key) + dir * d->inc) * 20.0) / 20.0, d->lo, d->hi));
        else mod->setSettingValue<int64_t>(key, std::clamp<int64_t>(optI(key) + (int64_t)(dir * d->inc), (int64_t)d->lo, (int64_t)d->hi));
        refreshVal(key); hudRefresh();
    }
    void onStepDown(CCObject* o) { step(o, -1); }
    void onStepUp(CCObject* o) { step(o, +1); }
    void onColor(CCObject* o) {
        int t = static_cast<CCNode*>(o)->getTag(), i = t / 2, dir = t % 2 ? 1 : -1; auto key = fmt::format("tier-{}", i);
        auto cur = tierColor(i); int idx = 0;
        for (int k = 0; k < kPalN; k++) if (kPalette[k].r == cur.r && kPalette[k].g == cur.g && kPalette[k].b == cur.b) idx = k;
        auto n = kPalette[(idx + dir + kPalN) % kPalN];
        if (Mod::get()->hasSetting(key)) Mod::get()->setSettingValue<ccColor3B>(key, n);
        paintSw(i); if (m_tierBars[i]) m_tierBars[i]->setColor(n); if (m_tierNum[i]) m_tierNum[i]->setColor(n);
        hudRefresh();
    }
    void onColorReset(CCObject*) {
        for (int i = 0; i < (int)kTierCount; i++) {
            auto key = fmt::format("tier-{}", i); auto& T = kTiers[i];
            if (Mod::get()->hasSetting(key)) Mod::get()->setSettingValue<ccColor3B>(key, ccColor3B{T.r, T.g, T.b});
            paintSw(i); if (m_tierBars[i]) m_tierBars[i]->setColor({T.r, T.g, T.b}); if (m_tierNum[i]) m_tierNum[i]->setColor({T.r, T.g, T.b});
        }
        hudRefresh();
    }

    void tick(float) {
        auto& s = State::get();
        m_status->setString(s.status.c_str()); m_status->limitLabelWidth(CW, 0.28f, 0.1f);
        m_barFill->setScaleX(CW * std::clamp(s.progress, 0.f, 1.f));
        int mx = 1; for (int v : s.summary.perTier) mx = std::max(mx, v);
        float bw = CW - 74 - 52;
        for (size_t i = 0; i < kTierCount; i++) {
            if (!m_tierBars[i]) continue;
            float target = s.summary.perTier[i] ? std::max(4.f, bw * s.summary.perTier[i] / (float)mx) : 0.f;
            m_tierBars[i]->setScaleX(m_tierBars[i]->getScaleX() + (target - m_tierBars[i]->getScaleX()) * 0.25f);
            m_tierNum[i]->setString(std::to_string(s.summary.perTier[i]).c_str());
        }
        if (m_summary) {
            m_summary->setString(s.summary.total ? fmt::format("{} clicks   tightest window {:.3f} f", s.summary.total, s.summary.tightest).c_str()
                                                 : "No analysis yet - load a macro and press ANALYZE");
            m_summary->limitLabelWidth(CW - 12, 0.3f, 0.1f);
        }
        if (m_xdInfo) {
            auto xd = Loader::get()->getLoadedMod("zilko.xdbot");
            m_xdInfo->setString(xd ? fmt::format("xdBot {} detected  |  folder: {}", xd->getVersion().toVString(), s.xdbotFolder().filename().string()).c_str()
                                   : "xdBot not detected (XFC still plays macros itself)");
            m_xdInfo->limitLabelWidth(CW - 24, 0.32f, 0.1f);
        }
    }

public:
    void closeFromOutside() { s_openPanel = nullptr; this->onClose(nullptr); }
    void onClose(CCObject* s) override { s_openPanel = nullptr; Popup::onClose(s); }

    static XFCPanel* create() {
        auto r = new XFCPanel();
        if (r && r->initAnchored(W, H, "square02_001.png")) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
};

void openPanel() {
    if (s_openPanel) {
        if (s_openPanel->getParent()) { s_openPanel->closeFromOutside(); return; }     // really on screen: the button toggles it
        s_openPanel = nullptr;                                                          // stale pointer: open a fresh one
    }
    auto p = XFCPanel::create();
    if (!p) {
        log::error("XFC: could not create the menu");
        FLAlertLayer::create("XFC", "The XFC menu could not be created. Please report this (Geode log has the details).", "OK")->show();
        return;
    }
    s_openPanel = p;
    p->show();
}

} // namespace xfc
