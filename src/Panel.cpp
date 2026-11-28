// XFC menu - dark "mod" UI: left sidebar with icons, rounded rows, live settings, macro search.
// Pages scroll WITHOUT any GL clipping (rows outside the window are simply hidden), so the content shows on every device
// and in every scene (main menu and in-level pause menu) and nothing can stick out of the panel.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <iterator>
#include <map>
#include <vector>
#include "XFC.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/TextInput.hpp>

namespace xfc {

class XFCPanel;
static XFCPanel* s_openPanel = nullptr;

namespace xui {
inline constexpr ccColor3B kBg{17, 16, 36}, kBorder{92, 80, 178}, kSide{25, 24, 52}, kRow{36, 34, 70}, kRowHi{66, 60, 124},
    kBtn{47, 44, 92}, kAcc{128, 108, 255}, kAcc2{176, 160, 255}, kText{240, 240, 255}, kDim{150, 148, 192},
    kGood{100, 238, 150}, kWarn{255, 210, 100}, kBad{255, 105, 115}, kOff{62, 59, 104}, kCyan{70, 225, 255};
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
// filled circle / ring outline as a polygon (drawDot is not reliable on every device)
inline void circleInto(CCDrawNode* d, float cx, float cy, float r, ccColor3B fill, float a = 1.f, float ringW = 0.f) {
    std::vector<CCPoint> p;
    for (int i = 0; i < 24; i++) { float t = (float)i * 6.2831853f / 24.f; p.push_back(ccp(cx + r * std::cos(t), cy + r * std::sin(t))); }
    d->drawPolygon(p.data(), (unsigned)p.size(), c4(fill, ringW > 0.f ? 0.f : a), ringW, c4(fill, ringW > 0.f ? 1.f : 0.f));
}
inline std::string lowerStr(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// A menu that ignores taps outside of its scroll window and turns a finger drag into scrolling.
// A tap on a button still presses it; as soon as the finger moves it becomes a scroll and the button is released unpressed.
class ClipMenu : public CCMenu {
public:
    CCNode* m_clip = nullptr;
    std::function<void(float)> m_scroll;
    CCPoint m_last{0, 0}, m_start{0, 0};
    bool m_drag = false, m_tracking = false, m_mine = false;
    static ClipMenu* create(CCNode* clip) {
        auto r = new ClipMenu();
        if (r && r->init()) { r->m_clip = clip; r->autorelease(); return r; }
        delete r; return nullptr;
    }
    bool ccTouchBegan(CCTouch* t, CCEvent* e) override {
        m_mine = false; m_drag = false; m_tracking = false;
        if (!m_clip || !this->isVisible()) return false;
        auto p = m_clip->convertTouchToNodeSpace(t); auto s = m_clip->getContentSize();
        if (p.x < 0 || p.y < 0 || p.x > s.width || p.y > s.height) return false;
        m_mine = true; m_last = m_start = p;
        m_tracking = CCMenu::ccTouchBegan(t, e);
        return true;                                           // we keep the touch even on empty space, so dragging works everywhere
    }
    void ccTouchMoved(CCTouch* t, CCEvent* e) override {
        if (!m_mine) return;
        auto p = m_clip->convertTouchToNodeSpace(t);
        if (!m_drag && std::abs(p.y - m_start.y) + std::abs(p.x - m_start.x) > 9.f) {
            m_drag = true;
            if (m_tracking) { CCMenu::ccTouchCancelled(t, e); m_tracking = false; }
        }
        if (m_drag) { if (m_scroll) m_scroll(p.y - m_last.y); }
        else if (m_tracking) CCMenu::ccTouchMoved(t, e);
        m_last = p;
    }
    void ccTouchEnded(CCTouch* t, CCEvent* e) override {
        if (m_tracking) CCMenu::ccTouchEnded(t, e);
        m_tracking = false; m_drag = false; m_mine = false;
    }
    void ccTouchCancelled(CCTouch* t, CCEvent* e) override {
        if (m_tracking) CCMenu::ccTouchCancelled(t, e);
        m_tracking = false; m_drag = false; m_mine = false;
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
    static constexpr float kEdge = 6.f;                                // a row may overhang the window by this much and still be shown

    struct Row { CCNode* node = nullptr; std::vector<CCNode*> items; float top = 0, bottom = 0; };
    struct Page { CCNode* view = nullptr; CCNode* body = nullptr; ClipMenu* menu = nullptr; std::vector<Row> rows;
                  float y = 0, w = 0, viewH = 0, scroll = 0, contentH = 0; };

    Page m_pages[NTABS];
    CCMenu* m_tabMenu = nullptr;
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
    CCLabelBMFont* m_count = nullptr;
    TextInput* m_search = nullptr;
    std::string m_query;
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
    // ON / OFF switch with the state written on it
    CCNode* pill(bool on) {
        auto n = CCNode::create(); n->setContentSize({52, 22});
        n->addChild(rrect(52, 22, 11, on ? kAcc : kOff, 1.f, on ? kAcc2 : kBorder, 1.f));
        auto k = CCDrawNode::create(); circleInto(k, on ? 41.f : 11.f, 11.f, 8.f, {255, 255, 255}); n->addChild(k);
        txt(on ? "ON" : "OFF", 0.26f, on ? ccColor3B{255, 255, 255} : kDim, {on ? 18.f : 34.f, 11.5f}, n, {0.5f, 0.5f});
        return n;
    }
    CCSprite* icon(const std::string& name, float size, CCPoint pos, CCNode* parent, int z = 2) {
        auto ic = CCSprite::create(name.c_str()); if (!ic) return nullptr;
        ic->setScale(size / std::max(1.f, std::max(ic->getContentWidth(), ic->getContentHeight())));
        ic->setPosition(pos); parent->addChild(ic, z); return ic;
    }
    // small vector icons for the tabs that have no picture of their own
    void tabIcon(int i, CCNode* parent, CCPoint c, ccColor3B col) {
        auto d = CCDrawNode::create(); parent->addChild(d, 2);
        if (i == 2) { circleInto(d, c.x, c.y, 6.5f, col, 1.f, 1.6f); circleInto(d, c.x, c.y, 2.2f, col); }                     // visuals: eye / ring
        else if (i == 3) { circleInto(d, c.x - 4.5f, c.y - 2.f, 3.4f, {255, 90, 100}); circleInto(d, c.x + 4.5f, c.y - 2.f, 3.4f, {90, 230, 130});
                           circleInto(d, c.x, c.y + 4.5f, 3.4f, {90, 160, 255}); }                                                 // colours: three dots
        else if (i == 4) { CCPoint a[3] = {ccp(c.x - 7, c.y - 5.5f), ccp(c.x - 7, c.y + 5.5f), ccp(c.x, c.y)};
                           CCPoint b[3] = {ccp(c.x, c.y - 5.5f), ccp(c.x, c.y + 5.5f), ccp(c.x + 7, c.y)};
                           d->drawPolygon(a, 3, c4(col), 0, c4(col, 0)); d->drawPolygon(b, 3, c4(col), 0, c4(col, 0)); }          // speed: double arrow
        else if (i == 5) { circleInto(d, c.x - 3.5f, c.y, 5.f, col, 1.f, 1.6f); circleInto(d, c.x + 3.5f, c.y, 5.f, col, 1.f, 1.6f); } // xdBot: linked rings
    }

    // ---------------------------------------------------------------- pages (own scrolling, no GL clipping)
    int prio() { return m_tabMenu ? m_tabMenu->getTouchPriority() : -128; }
    void applyScroll(Page& p) {
        if (!p.view) return;
        float maxS = std::max(0.f, p.contentH - p.viewH);
        p.scroll = std::clamp(p.scroll, 0.f, maxS);
        p.body->setPositionY(p.viewH + p.scroll);
        for (auto& r : p.rows) {
            float top = r.top + p.viewH + p.scroll, bot = r.bottom + p.viewH + p.scroll;
            bool vis = bot >= -kEdge && top <= p.viewH + kEdge;
            r.node->setVisible(vis);
            for (auto it : r.items) it->setVisible(vis);
        }
    }
    void scrollBy(int id, float dy) { auto& p = m_pages[id]; if (p.view) { p.scroll += dy; applyScroll(p); } }
    void onScrollUp(CCObject*) { scrollBy(m_tab, -kListH * 1.6f); }
    void onScrollDown(CCObject*) { scrollBy(m_tab, kListH * 1.6f); }

    void makePage(int id, CCNode* parent, float reserveTop = 0.f) {
        float oldScroll = m_pages[id].scroll;
        Page& p = m_pages[id]; p = Page(); p.scroll = oldScroll; p.w = CW; p.viewH = CH - reserveTop;
        p.view = CCNode::create(); p.view->setContentSize({CW, p.viewH}); p.view->setPosition(CX, FOOT); parent->addChild(p.view);
        p.body = CCNode::create(); p.view->addChild(p.body);
        p.menu = ClipMenu::create(p.view); p.menu->setPosition(0, 0); p.menu->setTouchPriority(prio());
        p.menu->m_scroll = [this, id](float dy) { scrollBy(id, dy); };
        p.body->addChild(p.menu, 5);
    }
    void endPage(int id) {
        auto& p = m_pages[id];
        p.contentH = std::max(p.viewH, -p.y + 4.f);
        applyScroll(p);
    }
    // a row of height h at the current position; returns its index
    int newRow(Page& p, float h, float gap) {
        Row r; r.top = p.y; r.bottom = p.y - h;
        r.node = CCNode::create(); r.node->setPosition({0, r.bottom}); r.node->setContentSize({p.w, h}); p.body->addChild(r.node);
        p.rows.push_back(r); p.y -= h + gap;
        return (int)p.rows.size() - 1;
    }
    float rowCY(Page& p, int r) { return p.rows[r].bottom + (p.rows[r].top - p.rows[r].bottom) / 2; }   // y inside the page (for menu items)
    void keep(Page& p, int r, CCNode* item) { p.rows[r].items.push_back(item); }
    void section(Page& p, const std::string& t) {
        int r = newRow(p, 18.f, 4.f);
        txt(t, 0.32f, kAcc2, {4, 9}, p.rows[r].node, {0.f, 0.5f}, p.w - 8);
    }
    void note(Page& p, const std::string& t, ccColor3B c = kDim) {
        int r = newRow(p, 13.f, 2.f);
        txt(t, 0.28f, c, {6, 6.5f}, p.rows[r].node, {0.f, 0.5f}, p.w - 12);
    }
    int beginRow(Page& p, const std::string& name, float h = kRowH, ccColor3B nameCol = kText) {
        int r = newRow(p, h, 5.f);
        p.rows[r].node->addChild(rrect(p.w, h, 9, kRow, 1.f, kRowHi, 0.8f));
        if (!name.empty()) txt(name, 0.38f, nameCol, {12, h / 2}, p.rows[r].node, {0.f, 0.5f}, p.w - 150.f);
        return r;
    }
    void addToggle(Page& p, const char* key, const char* name) {
        int r = beginRow(p, name); float cy = rowCY(p, r);
        auto t = CCMenuItemToggler::create(pill(false), pill(true), this, menu_selector(XFCPanel::onToggle));
        t->toggle(optB(key)); t->setPosition({p.w - 12 - 26, cy}); t->setTag((int)m_keys.size()); m_keys.push_back(key);
        p.menu->addChild(t); keep(p, r, t);
    }
    // [-] value [+]: the value label is centred in a fixed slot, so changing digits never moves anything
    void addStepper(Page& p, const char* key, const char* name, SEL_MenuHandler down, SEL_MenuHandler up) {
        int r = beginRow(p, name); float cy = rowCY(p, r); int id = (int)m_keys.size(); m_keys.push_back(key);
        keep(p, r, pillBtn(p.menu, "-", kAcc2, {p.w - 120, cy}, kSq, down, id));
        keep(p, r, pillBtn(p.menu, "+", kAcc2, {p.w - 24, cy}, kSq, up, id));
        m_vals[key] = txt("", 0.4f, kText, {p.w - 72, p.rows[r].node->getContentHeight() / 2}, p.rows[r].node, {0.5f, 0.5f}); refreshVal(key);
    }
    void addStep(Page& p, const char* key, const char* name) {
        addStepper(p, key, name, menu_selector(XFCPanel::onStepDown), menu_selector(XFCPanel::onStepUp));
    }
    void addPreset(Page& p, const char* key, const char* name, const int* arr, int n) {
        m_presets[key] = {arr, n};
        addStepper(p, key, name, menu_selector(XFCPanel::onPresetDown), menu_selector(XFCPanel::onPresetUp));
    }
    void addButtonRow(Page& p, const char* name, const char* btn, ccColor3B col, SEL_MenuHandler cb) {
        int r = beginRow(p, name); float cy = rowCY(p, r);
        keep(p, r, pillBtn(p.menu, btn, col, {p.w - 12 - 62, cy}, 124, cb));
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

        auto shadow = rrect(W + 8, H + 8, 20, {0, 0, 0}, 0.5f); shadow->setPosition({-4, -4}); m_mainLayer->addChild(shadow, -2);
        auto panel = rrect(W, H, 18, kBg, 1.f, kBorder, 1.8f); m_mainLayer->addChild(panel, -1);
        auto side = rrect(SW, H - 16, 14, kSide, 1.f, kRowHi, 0.8f); side->setPosition({8, 8}); m_mainLayer->addChild(side, 0);
        auto glow = rrect(W - 24, 2.f, 1.f, kAcc, 0.55f); glow->setPosition({12, H - 3.f}); m_mainLayer->addChild(glow, 1);   // thin accent line on top

        // header
        icon("xfc-circle.png"_spr, 36.f, {8 + 28.f, H - 34.f}, m_mainLayer);
        txt("XFC", 0.52f, kText, {8 + 52.f, H - 29.f}, m_mainLayer, {0.f, 0.5f}, 70.f);
        txt("Frame Counter", 0.26f, kDim, {8 + 52.f, H - 44.f}, m_mainLayer, {0.f, 0.5f}, 74.f);
        txt(fmt::format("by LNTB017  {}", Mod::get()->getVersion().toVString()), 0.23f, kDim, {8 + SW / 2, 20.f}, m_mainLayer, {0.5f, 0.5f}, SW - 16.f);

        // tabs (picture + name)
        const char* names[NTABS] = {"Macros", "Analysis", "Visuals", "Colours", "Speed", "xdBot", "Settings"};
        m_tabMenu = CCMenu::create(); m_tabMenu->setPosition(0, 0); m_mainLayer->addChild(m_tabMenu, 3);
        for (int i = 0; i < NTABS; i++) {
            auto n = CCNode::create(); n->setContentSize({SW - 20, kTabH});
            m_tabHi[i] = rrect(SW - 20, kTabH, 9, kRowHi, 1.f, kAcc, 1.f); n->addChild(m_tabHi[i]); m_tabHi[i]->setVisible(false);
            std::string pic;
            if (i == 0) pic = "xfc-circle.png"_spr; else if (i == 1) pic = "xfc-analyze.png"_spr; else if (i == 6) pic = "xfc-settings.png"_spr;
            if (!pic.empty()) icon(pic, 17.f, {16.f, kTabH / 2}, n);
            else tabIcon(i, n, {16.f, kTabH / 2}, kAcc2);
            m_tabLbl[i] = txt(names[i], 0.4f, kDim, {32, kTabH / 2}, n, {0.f, 0.5f}, SW - 58.f);
            auto b = CCMenuItemSpriteExtra::create(n, nullptr, this, menu_selector(XFCPanel::onTab));
            b->setTag(i); b->setPosition({8 + SW / 2, H - 80.f - i * 28.f}); m_tabMenu->addChild(b);
        }

        // title row + scroll arrows + close
        m_title = txt("", 0.62f, kText, {CX, H - 26.f}, m_mainLayer, {0.f, 0.5f}, CW - 150.f);
        auto top = CCMenu::create(); top->setPosition(0, 0); m_mainLayer->addChild(top, 30);
        {
            auto n = CCNode::create(); n->setContentSize({kCloseD, kCloseD});
            n->addChild(rrect(kCloseD, kCloseD, kCloseD / 2, kBtn, 1.f, kBorder, 1.2f));
            txt("x", 0.5f, kText, {kCloseD / 2, kCloseD / 2 + 0.5f}, n, {0.5f, 0.5f});
            auto b = CCMenuItemSpriteExtra::create(n, nullptr, this, menu_selector(XFCPanel::onCloseBtn));
            b->setPosition({W - 14.f - kCloseD / 2, H - 14.f - kCloseD / 2}); top->addChild(b);
            for (int dir = 0; dir < 2; dir++) {                       // scroll arrows: a second way to move long pages, besides dragging
                auto a = CCNode::create(); a->setContentSize({kCloseD, kCloseD});
                a->addChild(rrect(kCloseD, kCloseD, kCloseD / 2, kBtn, 1.f, kBorder, 1.2f));
                auto g = CCDrawNode::create(); float m = kCloseD / 2, k = 5.f;
                CCPoint up[3] = {ccp(m - k, m - k / 2), ccp(m + k, m - k / 2), ccp(m, m + k / 1.2f)};
                CCPoint dn[3] = {ccp(m - k, m + k / 2), ccp(m + k, m + k / 2), ccp(m, m - k / 1.2f)};
                g->drawPolygon(dir == 0 ? up : dn, 3, c4(kAcc2), 0, c4(kAcc2, 0)); a->addChild(g);
                auto ab = CCMenuItemSpriteExtra::create(a, nullptr, this, dir == 0 ? menu_selector(XFCPanel::onScrollUp) : menu_selector(XFCPanel::onScrollDown));
                ab->setPosition({W - 14.f - kCloseD / 2 - (kCloseD + 8.f) * (dir == 0 ? 2 : 1), H - 14.f - kCloseD / 2}); top->addChild(ab);
            }
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

        // search bar: type part of a macro name, the list filters while you type
        float sy = y - 31.f, sw = CW - 78.f;
        m_search = TextInput::create(sw / 0.85f, "Search macros...", font());
        m_search->setPosition({CX + sw / 2, sy}); m_search->setScale(0.85f);
        m_search->setCallback([this](std::string const& v) { m_query = lowerStr(v); queueList(); });
        m_tabs[0]->addChild(m_search, 4);
        m_count = txt("", 0.3f, kDim, {R - 2, sy}, m_tabs[0], {1.f, 0.5f}, 70.f);

        m_list = CCNode::create(); m_tabs[0]->addChild(m_list);
        fillList();
    }
    void fillList() {
        m_list->removeAllChildren(); m_files = State::get().findMacros();
        if (!State::get().macro.ok()) State::get().ensureMacro();      // remembered / first macro is loaded by itself
        makePage(0, m_list, 64.f);
        auto& p = m_pages[0];
        auto xdDir = State::get().xdbotFolder();
        size_t shown = 0;
        for (size_t i = 0; i < m_files.size(); i++) {
            auto name = m_files[i].filename().string();
            if (!m_query.empty() && lowerStr(name).find(m_query) == std::string::npos) continue;
            shown++;
            bool cur = m_files[i].string() == State::get().macroPath;
            float h = kListH;
            int r = newRow(p, h, 5.f); auto node = p.rows[r].node; float cy = rowCY(p, r);
            node->addChild(rrect(p.w, h, 10, cur ? kRowHi : kRow, 1.f, cur ? kAcc : kRowHi, cur ? 1.4f : 0.8f));
            txt(name, 0.36f, cur ? kText : ccColor3B{208, 208, 232}, {12, h / 2 + 7}, node, {0.f, 0.5f}, p.w - 200.f);
            bool fromXd = !xdDir.empty() && m_files[i].parent_path() == xdDir;
            std::string sub = fromXd ? "xdBot folder" : "XFC folder";
            ccColor3B subCol = fromXd ? kWarn : kDim;
            if (cur) {
                auto& s = State::get();
                sub = s.windows.empty() ? fmt::format("loaded  -  {} inputs  -  not analysed", s.macro.inputs.size())
                                        : fmt::format("loaded  -  analysed, {} clicks", s.summary.total);
                subCol = kGood;
            }
            txt(sub, 0.23f, subCol, {12, h / 2 - 9}, node, {0.f, 0.5f}, p.w - 200.f);
            keep(p, r, pillBtn(p.menu, "LOAD", cur ? kGood : kAcc2, {p.w - 8 - 44 - 4 - 62 - 4 - 23, cy}, 46, menu_selector(XFCPanel::onLoad), (int)i));
            keep(p, r, pillBtn(p.menu, "ANALYZE", kWarn, {p.w - 8 - 44 - 4 - 31, cy}, 62, menu_selector(XFCPanel::onRowAnalyze), (int)i));
            keep(p, r, pillBtn(p.menu, "PLAY", kGood, {p.w - 8 - 22, cy}, 44, menu_selector(XFCPanel::onRowPlay), (int)i));
        }
        if (m_files.empty()) {
            note(p, "No .gdr macros found.");
            note(p, "Press UPLOAD, or record one with xdBot (it is found automatically).");
        } else if (shown == 0) note(p, "No macro matches \"" + m_query + "\"");
        if (m_count) m_count->setString(m_query.empty() ? fmt::format("{} macros", m_files.size()).c_str() : fmt::format("{}/{}", shown, m_files.size()).c_str());
        endPage(0);
    }

    // ---- ANALYSIS
    void buildAnalysis() {
        makePage(1, m_tabs[1]); auto& p = m_pages[1];
        section(p, "CLICK WINDOWS (frames)");
        float barX = 74, barW = p.w - barX - 52;
        for (int i = (int)kTierCount - 1; i >= 0; i--) {
            int r = beginRow(p, "", 22.f); auto node = p.rows[r].node; float cy = 11.f;
            auto c = tierColor(i);
            txt(std::string(kTiers[i].label), 0.36f, c, {12, cy}, node, {0.f, 0.5f}, 56.f);
            auto track = rrect(barW, 6, 3, kBg); track->setPosition({barX, cy - 3}); node->addChild(track);
            m_tierBars[i] = CCLayerColor::create({c.r, c.g, c.b, 255}, 1, 6); m_tierBars[i]->setPosition({barX, cy - 3}); m_tierBars[i]->setScaleX(0.f); node->addChild(m_tierBars[i]);
            m_tierNum[i] = txt("0", 0.36f, c, {p.w - 12, cy}, node, {1.f, 0.5f});
        }
        int r = newRow(p, 18.f, 2.f);
        m_summary = txt("", 0.3f, kDim, {6, 9}, p.rows[r].node, {0.f, 0.5f}, p.w - 12);
        endPage(1);
    }

    // ---- VISUALS
    void buildVisuals() {
        makePage(2, m_tabs[2]); auto& p = m_pages[2];
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
        endPage(2);
    }

    // ---- COLOURS
    void paintSw(int i) { if (m_sw[i]) { m_sw[i]->clear(); roundInto(m_sw[i], 40, 16, 6, tierColor(i), 1.f, {255, 255, 255}, 1.f); } }
    void buildColors() {
        makePage(3, m_tabs[3]); auto& p = m_pages[3];
        section(p, "ROW COLOURS");
        for (int i = (int)kTierCount - 1; i >= 0; i--) {
            float h = kRowH - 2.f;
            int r = beginRow(p, std::string(kTiers[i].label) + " frames", h); float cy = rowCY(p, r);
            m_sw[i] = CCDrawNode::create(); m_sw[i]->setPosition({p.w - 138, h / 2 - 8}); p.rows[r].node->addChild(m_sw[i]); paintSw(i);
            keep(p, r, pillBtn(p.menu, "<", kAcc2, {p.w - 62, cy}, kSq, menu_selector(XFCPanel::onColor), i * 2));
            keep(p, r, pillBtn(p.menu, ">", kAcc2, {p.w - 24, cy}, kSq, menu_selector(XFCPanel::onColor), i * 2 + 1));
        }
        addButtonRow(p, "Back to default colours", "RESET", kWarn, menu_selector(XFCPanel::onColorReset));
        note(p, "Exact RGB: Geode > XFC > Settings");
        endPage(3);
    }

    // ---- SPEED
    void buildSpeed() {
        makePage(4, m_tabs[4]); auto& p = m_pages[4];
        section(p, "PHYSICS (TPS)");
        addToggle(p, "tps-enabled", "Custom TPS");
        addPreset(p, "tps", "TPS", kTpsPresets, (int)std::size(kTpsPresets));
        section(p, "VIDEO (FPS)");
        addToggle(p, "fps-enabled", "Custom FPS");
        addPreset(p, "fps", "FPS", kFpsPresets, (int)std::size(kFpsPresets));
        note(p, "Macros are recorded at 240 TPS - keep TPS at 240 when playing them.", kWarn);
        note(p, "Analysis always runs at 240 TPS internally, whatever you set here.");
        endPage(4);
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
        makePage(5, m_tabs[5]); auto& p = m_pages[5];
        section(p, "XDBOT LINK");
        int r = beginRow(p, "", kRowH); m_xdInfo = txt("", 0.32f, kText, {12, kRowH / 2}, p.rows[r].node, {0.f, 0.5f}, p.w - 24);
        addToggle(p, "link-xdbot", "Sync with xdBot playback");
        addButtonRow(p, "Copy macro into xdBot folder", "SEND", kAcc2, menu_selector(XFCPanel::onSend));
        note(p, "1. Load a .gdr in XFC and ANALYZE it once.");
        note(p, "2. SEND it to xdBot, then in xdBot: Load > Play.");
        note(p, "3. XFC follows the frame counter: rows, circles, frames and ms");
        note(p, "    fire on every analysed click. No xdBot changes needed.");
        endPage(5);
    }

    // ---- SETTINGS
    void buildSettings() {
        makePage(6, m_tabs[6]); auto& p = m_pages[6];
        section(p, "ANALYSIS");
        addStep(p, "max-shift", "Max window searched (f)");
        addStep(p, "horizon", "Survive-check length (f)");
        section(p, "SOUND");
        addToggle(p, "tier-sounds", "Sound on every click window");
        addStep(p, "sound-volume", "Volume");
        section(p, "MORE");
        addButtonRow(p, "Every setting with exact values", "GEODE", kWarn, menu_selector(XFCPanel::onGeode));
        note(p, "Works in every gamemode and dual: the real game physics are re-simulated.");
        endPage(6);
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
        if (m_search) m_search->setEnabled(i == 0);                      // the hidden search box must never catch taps on other tabs
    }
    void onTab(CCObject* o) { showTab(static_cast<CCNode*>(o)->getTag()); }
    void onCloseBtn(CCObject*) { this->onClose(nullptr); }
    void rebuildList(float) { this->unschedule(schedule_selector(XFCPanel::rebuildList)); fillList(); }
    void queueList() { this->scheduleOnce(schedule_selector(XFCPanel::rebuildList), 0.f); }   // never rebuild a menu from inside its own tap
    void onRefresh(CCObject*) { State::get().status = "Refreshed"; queueList(); }
    void onUpload(CCObject*) {
        file::FilePickOptions opts;                                        // no extension filter: phones often hide .gdr files behind one
        m_pick.bind([this](auto* ev) {
            auto r = ev->getValue(); if (!r || !r->isOk()) return;
            auto src = r->unwrap(); auto& s = State::get();
            auto name = src.filename().string();
            if (!endsWithGdr(name)) name += ".gdr";
            auto dst = s.xfcDir() / name;
            std::error_code ec; std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) { s.status = "Could not copy the file: " + ec.message(); return; }
            if (!s.loadMacro(dst)) std::filesystem::remove(dst, ec);          // not a macro: do not leave junk in the list
            queueList();
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
