#include "XFC.hpp"
#include <cmath>
#include <cctype>
#include <algorithm>

namespace xfc {

ccColor3B tierColor(int t) {
    t = std::clamp(t, 0, (int)kTierCount - 1);
    auto key = fmt::format("tier-{}", t);
    auto m = Mod::get();
    if (m->hasSetting(key)) {
        auto c = m->getSettingValue<ccColor3B>(key);
        if (c.r || c.g || c.b) return c;               // pure black is never a valid tier colour -> means "unset"
    }
    auto& T = kTiers[t];
    return {T.r, T.g, T.b};
}

Hud::~Hud() { if (State::get().hud == this) State::get().hud = nullptr; }

std::string fmtFrames(const Window& w) {
    bool sub = w.early + w.late + 1 < 2;
    return sub ? fmt::format("{:.3f}", w.frames) : fmt::format("{:.0f}", w.frames);
}

Hud* Hud::create() { auto r = new Hud(); if (r && r->init()) { r->autorelease(); return r; } delete r; return nullptr; }

bool Hud::init() {
    if (!CCNode::init()) return false;
    m_box = CCNode::create(); this->addChild(m_box);

    // everything is left-aligned (anchor x = 0) and the HUD node sits in the top-left corner of the screen
    m_frame = CCLabelBMFont::create("F 0", font());
    m_frame->setAnchorPoint({0, 1}); m_frame->setScale(0.5f); m_frame->setColor(kAccent2);
    m_box->addChild(m_frame);

    m_info = CCLabelBMFont::create("", font());
    m_info->setAnchorPoint({0, 1}); m_info->setScale(0.5f); m_info->setOpacity(0);
    m_box->addChild(m_info);

    m_stat = CCLabelBMFont::create("", font());
    m_stat->setAnchorPoint({0, 1}); m_stat->setScale(0.38f); m_stat->setOpacity(0);
    m_box->addChild(m_stat);

    for (size_t i = 0; i < kTierCount; i++) {
        auto l = CCLabelBMFont::create((std::string(kTiers[i].label) + ":").c_str(), font());
        l->setAnchorPoint({0, 1}); l->setScale(0.36f); l->setOpacity(235); m_box->addChild(l); m_rows[i] = l;
        auto n = CCLabelBMFont::create("0", font());
        n->setAnchorPoint({0, 1}); n->setScale(0.36f); n->setOpacity(235); m_box->addChild(n); m_nums[i] = n;
    }
    resetCounts(); relayout();
    return true;
}

void Hud::relayout() {
    auto mod = Mod::get();
    bool tiers = optB("show-tiers");
    bool showFrame = optB("show-frame");
    float sc = std::clamp((float)optD("hud-scale"), 0.5f, 2.f);
    auto win = CCDirector::get()->getWinSize();
    m_frame->setVisible(showFrame);

    // Rows exactly like the reference counter: widest window on top ("15-20:") down to "0-1:", names at x = 0 and the
    // counts left-aligned in their own column (x = 62), so growing digits never move anything.
    const float LINE = 11.5f;
    float y = 0;
    for (int i = (int)kTierCount - 1; i >= 0; i--) {
        m_rows[i]->setVisible(tiers); m_nums[i]->setVisible(tiers);
        m_rows[i]->setColor(tierColor(i)); m_nums[i]->setColor(tierColor(i));
        m_rows[i]->setPosition(0.f, y); m_nums[i]->setPosition(62.f, y);
        if (tiers) y -= LINE;
    }
    y -= 3.f;
    m_info->setPosition(0.f, y);  y -= 15.f;                 // last window: "3f  12.50ms"
    m_frame->setPosition(0.f, y); if (showFrame) y -= 17.f;  // frame counter
    m_stat->setPosition(0.f, y);                             // analysis progress / errors
    m_box->setScale(sc);
    this->setPosition(8.f, win.height - 6.f);       // always top-left
}

void Hud::setStatus(const std::string& text, ccColor3B col, float showSeconds) {
    if (!m_stat) return;
    m_stat->stopAllActions();
    m_stat->setString(text.c_str());
    m_stat->setColor(col);
    m_stat->limitLabelWidth(std::max(120.f, CCDirector::get()->getWinSize().width * 0.5f), 0.38f, 0.15f);
    m_stat->setOpacity(text.empty() ? 0 : 255);
    if (showSeconds > 0.f) m_stat->runAction(CCSequence::create(CCDelayTime::create(showSeconds), CCFadeTo::create(0.5f, 0), nullptr));
}

void Hud::resetCounts() {
    m_counts.fill(0);
    if (m_info) { m_info->stopAllActions(); m_info->setString(""); m_info->setOpacity(0); }
    for (size_t i = 0; i < kTierCount; i++) m_nums[i]->setString("0");
}

void Hud::setFrame(int f) { m_frame->setString(fmt::format("F {}", f).c_str()); }

void Hud::onClick(int tier, double frames, double ms, bool sub, double gap) {
    tier = std::clamp(tier, 0, (int)kTierCount - 1);
    m_counts[tier]++;
    m_nums[tier]->setString(std::to_string(m_counts[tier]).c_str());
    for (auto n : {(CCNode*)m_rows[tier], (CCNode*)m_nums[tier]}) {
        n->stopAllActions(); n->setScale(0.36f);
        n->runAction(CCSequence::create(CCEaseBackOut::create(CCScaleTo::create(0.08f, 0.5f)), CCEaseSineOut::create(CCScaleTo::create(0.25f, 0.36f)), nullptr));
    }
    auto txt = sub ? fmt::format("{:.3f}f", frames) : fmt::format("{:.0f}f", frames);
    if (optB("show-ms")) txt += fmt::format("  {:.3f}ms", ms);
    if (gap >= 0) txt += fmt::format("  gap {:.2f}u", gap);
    for (auto& ch : txt) ch = (char)std::toupper((unsigned char)ch);
    m_info->setString(txt.c_str()); m_info->setColor(tierColor(tier));
    m_info->stopAllActions(); m_info->setOpacity(255); m_info->setScale(0.7f);
    m_info->runAction(CCEaseBackOut::create(CCScaleTo::create(0.12f, 0.5f)));
    if (!optB("keep-last-window"))
        m_info->runAction(CCSequence::create(CCDelayTime::create(0.8f), CCFadeTo::create(0.5f, 0), nullptr));
}

// ------------------------------------------------------------------ rings

static std::vector<Ref<CCNode>> g_rings;

void clearRings() {
    for (auto& r : g_rings) if (r && r->getParent()) r->removeFromParent();
    g_rings.clear();
}

// Thick, very visible circle around the player at the exact moment of an input:
//   click   -> big ring in the colour of its window, "F<frame>" above and the window in frames below the number
//   release -> smaller white ring, "R<frame>" under the ring
void spawnRing(PlayLayer* pl, int frame, bool down, bool player2, const Window* w, double fps) {
    if (!pl || !pl->m_objectLayer) return;
    PlayerObject* who = (player2 && pl->m_player2) ? pl->m_player2 : pl->m_player1;
    if (!who) return;
    float sc = std::clamp((float)optD("ring-scale"), 0.2f, 3.f);
    double life = optD("ring-time");              // 0 = stay until the attempt resets
    ccColor3B c3 = (down && w) ? tierColor(w->tier) : ccColor3B{255, 255, 255};
    ccColor4F col{c3.r / 255.f, c3.g / 255.f, c3.b / 255.f, 1.f};

    auto node = CCNode::create();
    node->setPosition(who->getPosition());
    pl->m_objectLayer->addChild(node, 2000);
    std::erase_if(g_rings, [](auto& r) { return !r || !r->getParent(); });   // forget rings that already faded out
    g_rings.push_back(node);

    const int N = 48; float R = (down ? 27.f : 19.f) * sc, T = (down ? 5.5f : 4.5f) * sc;
    CCPoint pts[N];
    for (int i = 0; i < N; i++) pts[i] = ccp(R * std::cos(i * 6.2831853f / N), R * std::sin(i * 6.2831853f / N));
    auto draw = CCDrawNode::create();
    draw->drawPolygon(pts, N, {col.r, col.g, col.b, down ? 0.20f : 0.10f}, T, col);      // thick ring
    float R2 = R + T * 0.9f; CCPoint o[N];                                               // dark outline so it reads on any background
    for (int i = 0; i < N; i++) o[i] = ccp(R2 * std::cos(i * 6.2831853f / N), R2 * std::sin(i * 6.2831853f / N));
    draw->drawPolygon(o, N, {0, 0, 0, 0.f}, 1.5f * sc, {0, 0, 0, 0.75f});
    node->addChild(draw);

    if (optB("ring-label")) {
        auto label = [&](const std::string& t, float scale, CCPoint pos, ccColor3B c) {
            auto sh = CCLabelBMFont::create(t.c_str(), font());                           // black shadow: readable on white blocks too
            sh->setScale(scale); sh->setColor({0, 0, 0}); sh->setOpacity(220); sh->setPosition(pos + ccp(1.6f * sc, -1.6f * sc)); node->addChild(sh);
            auto l = CCLabelBMFont::create(t.c_str(), font());
            l->setScale(scale); l->setColor(c); l->setPosition(pos); node->addChild(l);
        };
        if (down) {
            label(fmt::format("F{}", frame), 0.62f * sc, {0, R + T + 10.f * sc}, c3);                       // exact frame of the click
            if (w) label(fmtFrames(*w) + " f", 0.40f * sc, {0, R + T + 25.f * sc}, c3);                    // its window
        } else {
            label(fmt::format("R{}", frame), 0.56f * sc, {0, -(R + T + 10.f * sc)}, c3);                   // exact frame of the release
        }
    }
    (void)fps;
    node->setScale(0.3f);
    node->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
    if (life > 0) node->runAction(CCSequence::create(CCDelayTime::create((float)life), CCFadeOut::create(0.3f), CCRemoveSelf::create(), nullptr));
}

} // namespace xfc
