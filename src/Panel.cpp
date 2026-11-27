// XFC menu: QOLMod-style on-screen menu. Left rail of tabs, animated content, live settings (no restart, no layout jumps).
#include "XFC.hpp"
#include <map>
#include <Geode/ui/GeodeUI.hpp>

namespace xfc {

static const ccColor3B kPalette[] = {
    {255, 45, 60}, {255, 140, 30}, {255, 225, 40}, {80, 230, 90}, {40, 220, 255}, {50, 130, 255},
    {170, 90, 255}, {255, 90, 200}, {255, 255, 255}, {150, 160, 175}, {0, 255, 170}, {255, 190, 120}};
static constexpr int kPalN = sizeof(kPalette) / sizeof(kPalette[0]);

class XFCPanel : public Popup<> {
protected:
    static constexpr float W = 500, H = 310, RAIL = 96;
    static constexpr int NTABS = 7;
    CCNode* m_tabs[NTABS]{};
    CCMenuItemSpriteExtra* m_tabBtn[NTABS]{};
    CCLabelBMFont* m_tabLbl[NTABS]{};
    CCLayerColor* m_indicator = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLayerColor* m_barFill = nullptr;
    std::array<CCLayerColor*, 7> m_tierBars{};
    std::array<CCLabelBMFont*, 7> m_tierNum{};
    CCLabelBMFont* m_summary = nullptr;
    CCLabelBMFont* m_xdInfo = nullptr;
    std::vector<std::string> m_keys;                   // toggle / stepper key table (by tag)
    std::map<std::string, CCLabelBMFont*> m_vals;      // value labels of steppers (fixed width, right aligned)
    std::vector<std::filesystem::path> m_files;
    CCNode* m_list = nullptr;
    int m_tab = 0;
    EventListener<Task<Result<std::filesystem::path>>> m_pick;

    // ---------------------------------------------------------------- widgets
    CCLabelBMFont* label(const std::string& t, float scale, ccColor3B c, CCPoint pos, CCNode* parent, CCPoint anchor = {0.f, 0.5f}) {
        auto l = CCLabelBMFont::create(t.c_str(), font()); l->setScale(scale); l->setColor(c); l->setPosition(pos); l->setAnchorPoint(anchor);
        if (anchor.x == 0.f) l->limitLabelWidth(std::max(30.f, W - pos.x - 16.f), scale, 0.12f);
        parent->addChild(l); return l;
    }
    CCNode* box(float w, float h, ccColor3B c, int op) {
        auto n = CCScale9Sprite::create("square02_001.png"); n->setContentSize({w, h}); n->setColor(c); n->setOpacity(op); return n;
    }
    CCMenuItemSpriteExtra* button(const std::string& t, ccColor3B col, CCPoint pos, float w, SEL_MenuHandler cb, CCNode* menu, int tag = 0) {
        auto bg = box(w, 26, {20, 44, 88}, 250);
        auto line = CCLayerColor::create({col.r, col.g, col.b, 255}, w - 10, 2); line->setPosition(5, 2); bg->addChild(line);
        auto txt = CCLabelBMFont::create(t.c_str(), font()); txt->setScale(0.42f); txt->limitLabelWidth(w - 8.f, 0.42f, 0.12f); txt->setColor(col); txt->setPosition({w / 2, 14}); bg->addChild(txt);
        auto b = CCMenuItemSpriteExtra::create(bg, nullptr, this, cb); b->setPosition(pos); b->setTag(tag); menu->addChild(b);
        b->setScale(0.7f); b->runAction(CCEaseBackOut::create(CCScaleTo::create(0.25f, 1.f)));
        return b;
    }
    CCNode* pill(bool on) {
        auto n = CCNode::create(); n->setContentSize({40, 20});
        auto bg = box(40, 20, on ? ccColor3B{0, 150, 255} : ccColor3B{30, 40, 60}, 255); bg->setAnchorPoint({0, 0}); n->addChild(bg);
        auto knob = box(14, 14, {235, 248, 255}, 255); knob->setPosition({on ? 27.f : 11.f, 10.f}); n->addChild(knob);
        return n;
    }
    void addToggle(CCNode* parent, CCNode* menu, const char* key, const char* name, float x, float y) {
        label(name, 0.4f, {210, 228, 250}, {x, y}, parent);
        auto t = CCMenuItemToggler::create(pill(false), pill(true), this, menu_selector(XFCPanel::onToggle));
        t->toggle(Mod::get()->getSettingValue<bool>(key));
        t->setPosition({x + 232, y}); t->setTag((int)m_keys.size()); m_keys.push_back(key); menu->addChild(t);
    }
    // - value + : value label has fixed width and is right aligned, so changing digits never moves anything
    void addStepper(CCNode* parent, CCNode* menu, const char* key, const char* name, float x, float y, bool isFloat) {
        label(name, 0.4f, {210, 228, 250}, {x, y}, parent);
        int id = (int)m_keys.size(); m_keys.push_back(key);
        button("-", kAccent, {x + 168, y}, 26, menu_selector(XFCPanel::onStepDown), menu, id);
        button("+", kAccent, {x + 250, y}, 26, menu_selector(XFCPanel::onStepUp), menu, id);
        m_vals[key] = label("", 0.42f, kAccent2, {x + 232, y}, parent, {1.f, 0.5f});
        m_vals[key]->setUserObject("f", CCBool::create(isFloat)); refreshVal(key);
    }
    void addSlider(CCNode* parent, CCNode* menu, const char* key, const char* name, float x, float y, float lo, float hi) {
        label(name, 0.4f, {210, 228, 250}, {x, y}, parent);
        auto s = Slider::create(this, menu_selector(XFCPanel::onSlider), 0.55f);
        s->setPosition({x + 200, y}); s->getThumb()->setTag((int)m_keys.size()); m_keys.push_back(key);
        double v = Mod::get()->getSettingValue<double>(key); s->setValue((float)((v - lo) / (hi - lo)));
        m_ranges[key] = {lo, hi}; menu->addChild(s);
    }
    std::map<std::string, std::pair<float, float>> m_ranges;

    void refreshVal(const std::string& key) {
        auto l = m_vals[key]; bool f = static_cast<CCBool*>(l->getUserObject("f"))->getValue();
        auto mod = Mod::get();
        l->setString((f ? fmt::format("{:.1f}", mod->getSettingValue<double>(key)) : std::to_string(mod->getSettingValue<int64_t>(key))).c_str());
    }
    void hudRefresh() {
        if (auto h = State::get().hud) { h->setVisible(Mod::get()->getSettingValue<bool>("show-hud")); h->relayout(); }
    }

    // ---------------------------------------------------------------- setup
    bool setup() override {
        this->setTitle("");
        auto sz = m_mainLayer->getContentSize();
        m_bgSprite->setColor(kPanelBg); m_bgSprite->setOpacity(240);

        // glow header + rail
        auto glow = CCLayerGradient::create({0, 170, 255, 80}, {0, 0, 0, 0}, {0, -1}); glow->setContentSize({sz.width - 10, 70}); glow->setPosition(5, sz.height - 75); m_mainLayer->addChild(glow);
        auto rail = box(RAIL - 8, sz.height - 24, {8, 16, 34}, 235); rail->setAnchorPoint({0, 0}); rail->setPosition(10, 12); m_mainLayer->addChild(rail);
        auto edge = CCLayerColor::create({0, 170, 255, 255}, 2, sz.height - 40); edge->setPosition(RAIL + 6, 20); m_mainLayer->addChild(edge, 2);
        edge->setScaleY(0); edge->runAction(CCEaseExponentialOut::create(CCScaleTo::create(0.6f, 1.f, 1.f)));

        auto icon = CCSprite::create("xfc-icon.png"_spr); icon->setScale(0.42f); icon->setPosition({RAIL / 2 + 6, sz.height - 40}); m_mainLayer->addChild(icon, 3);
        icon->setScale(0.f); icon->runAction(CCEaseElasticOut::create(CCScaleTo::create(0.7f, 0.42f), 0.6f));
        label("XFC", 0.62f, kAccent2, {RAIL / 2 + 6, sz.height - 72}, m_mainLayer, {0.5f, 0.5f});
        label("v2.3.0", 0.28f, {110, 150, 200}, {RAIL / 2 + 6, sz.height - 86}, m_mainLayer, {0.5f, 0.5f});

        const char* names[NTABS] = {"MACROS", "ANALYSIS", "VISUALS", "COLORS", "SPEED", "XDBOT", "SETTINGS"};
        auto tm = CCMenu::create(); tm->setPosition(0, 0); m_mainLayer->addChild(tm, 3);
        for (int i = 0; i < NTABS; i++) {
            m_tabLbl[i] = CCLabelBMFont::create(names[i], font()); m_tabLbl[i]->setScale(0.42f); m_tabLbl[i]->limitLabelWidth(RAIL - 20.f, 0.42f, 0.15f);
            auto wrap = CCNode::create(); wrap->setContentSize({RAIL - 16, 24}); m_tabLbl[i]->setPosition({(RAIL - 16) / 2, 12}); wrap->addChild(m_tabLbl[i]);
            m_tabBtn[i] = CCMenuItemSpriteExtra::create(wrap, this, menu_selector(XFCPanel::onTab));
            m_tabBtn[i]->setTag(i); m_tabBtn[i]->setPosition({RAIL / 2 + 6, sz.height - 118 - i * 27.f}); tm->addChild(m_tabBtn[i]);
            m_tabBtn[i]->setScale(0.f); m_tabBtn[i]->runAction(CCSequence::create(CCDelayTime::create(0.05f * i), CCEaseBackOut::create(CCScaleTo::create(0.25f, 1.f)), nullptr));
        }
        m_indicator = CCLayerColor::create({0, 230, 255, 255}, 3, 20); m_mainLayer->addChild(m_indicator, 4);

        m_status = label(State::get().status, 0.32f, {140, 190, 235}, {RAIL + 18, 16}, m_mainLayer);
        for (int i = 0; i < NTABS; i++) { m_tabs[i] = CCNode::create(); m_tabs[i]->setContentSize(sz); m_mainLayer->addChild(m_tabs[i], 1); }
        buildMacros(); buildAnalysis(); buildVisuals(); buildColors(); buildSpeed(); buildXdbot(); buildSettings();
        showTab(0, false);
        this->schedule(schedule_selector(XFCPanel::tick), 0.05f);
        return true;
    }

    float cx() const { return RAIL + 20; }
    float top() const { return m_mainLayer->getContentSize().height - 30; }
    float right() const { return m_mainLayer->getContentSize().width - 22; }

    // ---- MACROS
    void buildMacros() {
        auto& s = State::get(); float T = top(), R = right(), x0 = cx();
        auto menu = CCMenu::create(); menu->setPosition(0, 0); m_tabs[0]->addChild(menu);
        button("UPLOAD .GDR", kAccent2, {x0 + 56, T}, 112, menu_selector(XFCPanel::onUpload), menu);
        button("REFRESH", kAccent, {x0 + 150, T}, 70, menu_selector(XFCPanel::onRefresh), menu);
        button("PLAY", {80, 235, 120}, {R - 224, T}, 54, menu_selector(XFCPanel::onPlay), menu);
        button("STOP", {255, 80, 90}, {R - 164, T}, 54, menu_selector(XFCPanel::onStop), menu);
        button("ANALYZE", {255, 225, 40}, {R - 78, T}, 90, menu_selector(XFCPanel::onAnalyze), menu);
        m_list = CCNode::create(); m_tabs[0]->addChild(m_list);
        fillList();
    }
    void fillList() {
        m_list->removeAllChildren(); m_files = State::get().findMacros();
        auto sz = m_mainLayer->getContentSize(); float lw = right() - cx() + 10, lh = sz.height - 100;
        auto scroll = ScrollLayer::create({lw, lh}); scroll->setPosition(cx() - 6, 30); m_list->addChild(scroll);
        float rowH = 30, h = std::max<float>(lh, m_files.size() * rowH);
        scroll->m_contentLayer->setContentSize({lw, h});
        auto lm = CCMenu::create(); lm->setPosition(0, 0); lm->setContentSize({lw, h}); scroll->m_contentLayer->addChild(lm);
        if (m_files.empty()) label("No .gdr macros - press UPLOAD or drop them in the xdBot macros folder", 0.32f, {120, 150, 190}, {6, h - 16}, scroll->m_contentLayer);
        for (size_t i = 0; i < m_files.size(); i++) {
            float y = h - rowH * i - 16;
            auto row = box(lw - 6, 26, {12, 28, 56}, 0); row->setPosition({lw / 2, y}); scroll->m_contentLayer->addChild(row);
            row->runAction(CCSequence::create(CCDelayTime::create(0.04f * i), CCFadeTo::create(0.25f, 200), nullptr));
            bool cur = m_files[i].string() == State::get().macroPath;
            auto nm = label(m_files[i].filename().string(), 0.34f, cur ? kAccent2 : ccColor3B{200, 220, 245}, {8, y + 3}, scroll->m_contentLayer);
            nm->limitLabelWidth(lw - 215, 0.34f, 0.1f);
            bool fromXd = !State::get().xdbotFolder().empty() && m_files[i].parent_path() == State::get().xdbotFolder();
            label(fromXd ? "xdBot" : "XFC", 0.24f, fromXd ? ccColor3B{255, 190, 120} : ccColor3B{110, 150, 200}, {8, y - 8}, scroll->m_contentLayer);
            button(cur ? "LOADED" : "LOAD", cur ? ccColor3B{80, 235, 120} : kAccent, {lw - 166, y}, 56, menu_selector(XFCPanel::onLoad), lm, (int)i);
            button("ANALYZE", {255, 225, 40}, {lw - 100, y}, 70, menu_selector(XFCPanel::onRowAnalyze), lm, (int)i);
            button("PLAY", {80, 235, 120}, {lw - 38, y}, 46, menu_selector(XFCPanel::onRowPlay), lm, (int)i);
        }
        scroll->moveToTop();
    }

    // ---- ANALYSIS
    void buildAnalysis() {
        float T = top(), R = right(), x0 = cx(); float bw = R - x0 - 120;
        label("CLICK WINDOW TIERS", 0.42f, kAccent2, {x0, T}, m_tabs[1]);
        for (int i = 0; i < 7; i++) {
            float y = T - 30 - i * 22;
            label(kTiers[i].label, 0.4f, tierColor(i), {x0, y}, m_tabs[1]);
            auto track = CCLayerColor::create({20, 34, 62, 255}, bw, 8); track->setPosition(x0 + 70, y - 4); m_tabs[1]->addChild(track);
            auto c = tierColor(i);
            m_tierBars[i] = CCLayerColor::create({c.r, c.g, c.b, 255}, 1, 8); m_tierBars[i]->setPosition(x0 + 70, y - 4); m_tabs[1]->addChild(m_tierBars[i]);
            m_tierNum[i] = label("0", 0.4f, c, {R, y}, m_tabs[1], {1.f, 0.5f});
        }
        m_summary = label("", 0.36f, {160, 200, 240}, {x0, T - 196}, m_tabs[1]);
        auto track = CCLayerColor::create({20, 34, 62, 255}, R - x0, 6); track->setPosition(x0, 38); m_tabs[1]->addChild(track);
        m_barFill = CCLayerColor::create({0, 230, 255, 255}, 1, 6); m_barFill->setPosition(x0, 38); m_tabs[1]->addChild(m_barFill);
    }

    // ---- VISUALS
    void buildVisuals() {
        float T = top(), x0 = cx(), y = T;
        auto menu = CCMenu::create(); menu->setPosition(0, 0); m_tabs[2]->addChild(menu);
        label("HUD", 0.42f, kAccent2, {x0, y}, m_tabs[2]);
        addToggle(m_tabs[2], menu, "show-hud", "Show HUD", x0, y -= 24);
        addToggle(m_tabs[2], menu, "show-tiers", "Tier list (7 colours)", x0, y -= 24);
        addToggle(m_tabs[2], menu, "show-ms", "Window in ms", x0, y -= 24);
        addSlider(m_tabs[2], menu, "hud-scale", "HUD scale", x0, y -= 24, 0.5f, 2.f);
        const char* c[4] = {"Top Left", "Top Right", "Bottom Left", "Bottom Right"};
        for (int i = 0; i < 4; i++) button(c[i], kAccent, {x0 + 36 + i * 76.f, y -= (i == 0 ? 26 : 0)}, 70, menu_selector(XFCPanel::onCorner), menu, i);
        label("CLICK CIRCLES", 0.42f, kAccent2, {x0, y -= 30}, m_tabs[2]);
        addToggle(m_tabs[2], menu, "show-rings", "Circle on every click", x0, y -= 24);
        addToggle(m_tabs[2], menu, "ring-label", "Frames above circle", x0, y -= 24);
        addSlider(m_tabs[2], menu, "ring-scale", "Circle size", x0, y -= 24, 0.4f, 2.5f);
        addStepper(m_tabs[2], menu, "ring-time", "Lifetime (s, 0 = stay)", x0, y -= 24, true);
    }

    // ---- COLORS
    void buildColors() {
        float T = top(), x0 = cx();
        auto menu = CCMenu::create(); menu->setPosition(0, 0); m_tabs[3]->addChild(menu);
        label("TIER COLOURS", 0.42f, kAccent2, {x0, T}, m_tabs[3]);
        for (int i = 0; i < 7; i++) {
            float y = T - 30 - i * 26;
            label(fmt::format("{}   ({} f)", i + 1, kTiers[i].label), 0.4f, {210, 228, 250}, {x0, y}, m_tabs[3]);
            auto c = tierColor(i); auto sw = CCLayerColor::create({c.r, c.g, c.b, 255}, 60, 14); sw->setPosition(x0 + 150, y - 7); sw->setTag(100 + i); m_tabs[3]->addChild(sw);
            button("<", kAccent, {x0 + 240, y}, 28, menu_selector(XFCPanel::onColor), menu, i * 2);
            button(">", kAccent, {x0 + 274, y}, 28, menu_selector(XFCPanel::onColor), menu, i * 2 + 1);
        }
        button("RESET COLOURS", {255, 225, 40}, {x0 + 90, T - 30 - 7 * 26 - 4}, 150, menu_selector(XFCPanel::onColorReset), menu);
        label("Exact RGB: Geode > XFC > Settings", 0.3f, {110, 140, 180}, {x0 + 180, T - 30 - 7 * 26 - 4}, m_tabs[3]);
    }

    // ---- SPEED (TPS / FPS)
    void buildSpeed() {
        float T = top(), x0 = cx(), y = T;
        auto menu = CCMenu::create(); menu->setPosition(0, 0); m_tabs[4]->addChild(menu);
        label("PHYSICS (TPS)", 0.42f, kAccent2, {x0, y}, m_tabs[4]);
        addToggle(m_tabs[4], menu, "tps-enabled", "Custom TPS", x0, y -= 26);
        addPreset(m_tabs[4], menu, "tps", "TPS", x0, y -= 26, kTpsPresets, (int)std::size(kTpsPresets));
        label("VIDEO (FPS)", 0.42f, kAccent2, {x0, y -= 36}, m_tabs[4]);
        addToggle(m_tabs[4], menu, "fps-enabled", "Custom FPS", x0, y -= 26);
        addPreset(m_tabs[4], menu, "fps", "FPS", x0, y -= 26, kFpsPresets, (int)std::size(kFpsPresets));
        label("Macros are recorded at 240 TPS - keep TPS at 240 when playing them.", 0.3f, {255, 190, 120}, {x0, y -= 34}, m_tabs[4]);
        label("Analysis always runs at 240 TPS internally, whatever you set here.", 0.3f, {110, 140, 180}, {x0, y -= 18}, m_tabs[4]);
    }
    std::map<std::string, std::pair<const int*, int>> m_presets;
    void addPreset(CCNode* parent, CCNode* menu, const char* key, const char* name, float x, float y, const int* arr, int n) {
        label(name, 0.4f, {210, 228, 250}, {x, y}, parent);
        int id = (int)m_keys.size(); m_keys.push_back(key); m_presets[key] = {arr, n};
        button("-", kAccent, {x + 168, y}, 26, menu_selector(XFCPanel::onPresetDown), menu, id);
        button("+", kAccent, {x + 250, y}, 26, menu_selector(XFCPanel::onPresetUp), menu, id);
        m_vals[key] = label("", 0.42f, kAccent2, {x + 232, y}, parent, {1.f, 0.5f});
        m_vals[key]->setUserObject("f", CCBool::create(false)); refreshVal(key);
    }
    void preset(CCObject* o, int dir) {
        auto key = m_keys[static_cast<CCNode*>(o)->getTag()]; auto [arr, n] = m_presets[key];
        int64_t cur = Mod::get()->getSettingValue<int64_t>(key); int idx = 0;
        for (int i = 0; i < n; i++) if (arr[i] <= cur) idx = i;
        idx = std::clamp(idx + dir, 0, n - 1);
        Mod::get()->setSettingValue<int64_t>(key, arr[idx]); refreshVal(key);
        if (key == "fps") applyFps();
    }
    void onPresetDown(CCObject* o) { preset(o, -1); }
    void onPresetUp(CCObject* o) { preset(o, +1); }

    // ---- XDBOT
    void buildXdbot() {
        float T = top(), x0 = cx(), y = T;
        auto menu = CCMenu::create(); menu->setPosition(0, 0); m_tabs[5]->addChild(menu);
        label("XDBOT LINK", 0.42f, kAccent2, {x0, y}, m_tabs[5]);
        m_xdInfo = label("", 0.36f, {160, 200, 240}, {x0, y -= 24}, m_tabs[5]);
        addToggle(m_tabs[5], menu, "link-xdbot", "Sync with xdBot playback", x0, y -= 28);
        button("SEND MACRO TO XDBOT", kAccent2, {x0 + 100, y -= 32}, 200, menu_selector(XFCPanel::onSend), menu);
        const char* how[] = {"1. Load a .gdr in XFC, ANALYZE it once.", "2. SEND MACRO TO XDBOT, then in xdBot: Load > Play.",
                             "3. XFC follows the frame counter: circles, frames,", "    ms and tier counters fire on every analysed click.",
                             "xdBot has no public API, so the link is the shared", "macro folder + frame sync (no xdBot changes needed)."};
        for (auto t : how) label(t, 0.32f, {140, 170, 210}, {x0, y -= 20}, m_tabs[5]);
    }

    // ---- SETTINGS
    void buildSettings() {
        float T = top(), x0 = cx(), y = T;
        auto menu = CCMenu::create(); menu->setPosition(0, 0); m_tabs[6]->addChild(menu);
        label("ANALYSIS", 0.42f, kAccent2, {x0, y}, m_tabs[6]);
        addStepper(m_tabs[6], menu, "max-shift", "Max window searched (f)", x0, y -= 26, false);
        addStepper(m_tabs[6], menu, "horizon", "Survive-check length (f)", x0, y -= 26, false);
        label("INTERFACE", 0.42f, kAccent2, {x0, y -= 34}, m_tabs[6]);
        addToggle(m_tabs[6], menu, "show-x-button", "X button in-level", x0, y -= 26);
        button("ALL GEODE SETTINGS", {255, 225, 40}, {x0 + 90, y -= 36}, 170, menu_selector(XFCPanel::onGeode), menu);
        label("Supports every gamemode & dual: the real game physics are re-simulated.", 0.3f, {110, 140, 180}, {x0, y -= 28}, m_tabs[6]);
    }

    // ---------------------------------------------------------------- behaviour
    void showTab(int i, bool animate = true) {
        m_tab = i; auto sz = m_mainLayer->getContentSize();
        for (int t = 0; t < NTABS; t++) {
            m_tabs[t]->stopAllActions(); m_tabs[t]->setVisible(t == i);
            m_tabLbl[t]->setColor(t == i ? kAccent2 : ccColor3B{130, 160, 200});
            if (t == i && animate) { m_tabs[t]->setPosition(16, 0); m_tabs[t]->runAction(CCEaseExponentialOut::create(CCMoveTo::create(0.3f, {0, 0}))); }
            else if (t == i) m_tabs[t]->setPosition(0, 0);
        }
        m_indicator->stopAllActions();
        m_indicator->runAction(CCEaseExponentialOut::create(CCMoveTo::create(animate ? 0.25f : 0.f, {12, sz.height - 118 - i * 27.f - 10})));
    }
    void onTab(CCObject* o) { showTab(static_cast<CCNode*>(o)->getTag()); }
    void onRefresh(CCObject*) { State::get().status = "Refreshed"; fillList(); }
    void onUpload(CCObject*) {
        file::FilePickOptions opts; opts.filters.push_back({"GD replay", {"*.gdr"}});
        m_pick.bind([this](auto* ev) {
            auto r = ev->getValue(); if (!r || !r->isOk()) return;
            auto src = r->unwrap(); auto& s = State::get();
            if (src.extension() != ".gdr") { s.status = "Only .gdr files are accepted"; return; }
            auto dst = s.xfcDir() / src.filename();
            std::error_code ec; std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            s.loadMacro(dst); fillList();
        });
        m_pick.setFilter(file::pick(file::PickMode::OpenFile, opts));
    }
    void onLoad(CCObject* o) { auto i = static_cast<CCNode*>(o)->getTag(); if (i < (int)m_files.size()) { State::get().loadMacro(m_files[i]); fillList(); } }
    // one tap: load that macro, analyse it (or reuse cache), and (PLAY) run it with the frame windows
    void onRowAnalyze(CCObject* o) { auto i = static_cast<CCNode*>(o)->getTag(); if (i < (int)m_files.size() && State::get().loadMacro(m_files[i])) { fillList(); State::get().startAnalysis(); showTab(1); } }
    void onRowPlay(CCObject* o) {
        auto i = static_cast<CCNode*>(o)->getTag();
        if (i < (int)m_files.size() && State::get().loadMacro(m_files[i])) { State::get().analyzeAndPlay(); if (State::get().playing) this->onClose(nullptr); else { fillList(); if (State::get().simulating) showTab(1); } }
    }
    void onPlay(CCObject*) { State::get().analyzeAndPlay(); if (State::get().playing) this->onClose(nullptr); else if (State::get().simulating) showTab(1); }
    void onStop(CCObject*) { State::get().stopPlayback(); }
    void onAnalyze(CCObject*) { State::get().startAnalysis(); showTab(1); }
    void onSend(CCObject*) { State::get().sendToXdbot(); }
    void onGeode(CCObject*) { geode::openSettingsPopup(Mod::get()); }

    void onToggle(CCObject* o) {
        auto t = static_cast<CCMenuItemToggler*>(o); auto key = m_keys[t->getTag()];
        Mod::get()->setSettingValue<bool>(key, !t->isToggled());      // isToggled() is the state before the tap
        if (key == "fps-enabled") { if (!t->isToggled()) applyFps(); else CCDirector::get()->setAnimationInterval(1.0 / 60.0); }
        hudRefresh();
    }
    void onSlider(CCObject* o) {
        auto th = static_cast<SliderThumb*>(o); auto key = m_keys[th->getTag()]; auto [lo, hi] = m_ranges[key];
        Mod::get()->setSettingValue<double>(key, std::round((lo + th->getValue() * (hi - lo)) * 20.0) / 20.0);
        hudRefresh();
    }
    void step(CCObject* o, int dir) {
        auto key = m_keys[static_cast<CCNode*>(o)->getTag()]; auto mod = Mod::get();
        if (key == "ring-time") mod->setSettingValue<double>(key, std::clamp(mod->getSettingValue<double>(key) + dir * 0.5, 0.0, 8.0));
        else if (key == "max-shift") mod->setSettingValue<int64_t>(key, std::clamp<int64_t>(mod->getSettingValue<int64_t>(key) + dir, 5, 30));
        else mod->setSettingValue<int64_t>(key, std::clamp<int64_t>(mod->getSettingValue<int64_t>(key) + dir * 10, 30, 240));
        refreshVal(key);
    }
    void onStepDown(CCObject* o) { step(o, -1); }
    void onStepUp(CCObject* o) { step(o, +1); }
    void onCorner(CCObject* o) {
        const char* c[4] = {"Top Left", "Top Right", "Bottom Left", "Bottom Right"};
        Mod::get()->setSettingValue<std::string>("hud-corner", c[static_cast<CCNode*>(o)->getTag()]); hudRefresh();
    }
    void onColor(CCObject* o) {
        int t = static_cast<CCNode*>(o)->getTag(), i = t / 2, dir = t % 2 ? 1 : -1; auto key = fmt::format("tier-{}", i);
        auto cur = tierColor(i); int idx = 0;
        for (int k = 0; k < kPalN; k++) if (kPalette[k].r == cur.r && kPalette[k].g == cur.g && kPalette[k].b == cur.b) idx = k;
        auto n = kPalette[(idx + dir + kPalN) % kPalN];
        Mod::get()->setSettingValue<ccColor3B>(key, n);
        if (auto sw = static_cast<CCLayerColor*>(m_tabs[3]->getChildByTag(100 + i))) sw->setColor(n);
        hudRefresh();
    }
    void onColorReset(CCObject*) {
        for (int i = 0; i < 7; i++) {
            Mod::get()->setSettingValue<ccColor3B>(fmt::format("tier-{}", i), kPalette[i]);
            if (auto sw = static_cast<CCLayerColor*>(m_tabs[3]->getChildByTag(100 + i))) sw->setColor(kPalette[i]);
        }
        hudRefresh();
    }

    void tick(float) {
        auto& s = State::get(); float R = right(), x0 = cx();
        m_status->setString(s.status.c_str()); m_status->limitLabelWidth(R - x0 + 10, 0.32f, 0.1f);
        m_barFill->setScaleX((R - x0) * s.progress);
        int mx = 1; for (int v : s.summary.perTier) mx = std::max(mx, v);
        float bw = R - x0 - 120;
        for (int i = 0; i < 7; i++) {
            float target = std::max(1.f, bw * s.summary.perTier[i] / (float)mx);
            m_tierBars[i]->setScaleX(m_tierBars[i]->getScaleX() + (target - m_tierBars[i]->getScaleX()) * 0.25f);
            m_tierNum[i]->setString(std::to_string(s.summary.perTier[i]).c_str());
        }
        m_summary->setString(s.summary.total ? fmt::format("{} clicks   tightest window {:.3f} f", s.summary.total, s.summary.tightest).c_str() : "No analysis yet - load a macro, enter its level, press ANALYZE");
        auto xd = Loader::get()->getLoadedMod("zilko.xdbot");
        m_xdInfo->setString(xd ? fmt::format("xdBot {} detected  |  folder: {}", xd->getVersion().toVString(), State::get().xdbotFolder().filename().string()).c_str() : "xdBot not detected (XFC still plays macros itself)");
        m_xdInfo->limitLabelWidth(R - x0, 0.36f, 0.1f);
    }

public:
    static XFCPanel* create() {
        auto r = new XFCPanel();
        if (r && r->initAnchored(W, H, "square02_001.png")) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
};

void openPanel() { if (auto p = XFCPanel::create()) p->show(); }

} // namespace xfc
