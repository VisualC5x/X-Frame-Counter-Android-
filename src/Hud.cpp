#include "XFC.hpp"
#include <cmath>

namespace xfc {

ccColor3B tierColor(int t) { return Mod::get()->getSettingValue<ccColor3B>(fmt::format("tier-{}", t)); }

std::string fmtFrames(const Window& w) {
    bool sub = w.early + w.late + 1 < 2;
    return sub ? fmt::format("{:.3f}", w.frames) : fmt::format("{:.0f}", w.frames);
}

Hud* Hud::create() { auto r = new Hud(); if (r && r->init()) { r->autorelease(); return r; } delete r; return nullptr; }

bool Hud::init() {
    if (!CCNode::init()) return false;
    m_box = CCNode::create(); this->addChild(m_box);

    m_frame = CCLabelBMFont::create("F 0", font());
    m_frame->setAnchorPoint({1, 1}); m_frame->setScale(0.62f); m_frame->setColor(kAccent2);
    m_box->addChild(m_frame);

    m_info = CCLabelBMFont::create("", font());
    m_info->setAnchorPoint({1, 1}); m_info->setScale(0.5f); m_info->setOpacity(0);
    m_box->addChild(m_info);

    for (int i = 0; i < 7; i++) {
        auto l = CCLabelBMFont::create(kTiers[i].label, font());
        l->setAnchorPoint({0, 1}); l->setScale(0.4f); l->setOpacity(215); m_box->addChild(l); m_rows[i] = l;
        auto n = CCLabelBMFont::create("0", font());
        n->setAnchorPoint({1, 1}); n->setScale(0.4f); n->setOpacity(215); m_box->addChild(n); m_nums[i] = n;
    }
    resetCounts(); relayout();
    return true;
}

void Hud::relayout() {
    auto mod = Mod::get();
    bool tiers = mod->getSettingValue<bool>("show-tiers");
    auto corner = mod->getSettingValue<std::string>("hud-corner");
    float sc = (float)mod->getSettingValue<double>("hud-scale");
    auto win = CCDirector::get()->getWinSize();
    bool top = corner.starts_with("Top"), right = corner.ends_with("Right");

    // fixed-width column: names at x = -70, counts at x = 0 (right aligned) -> digits can never push anything around
    float y = 0;
    m_frame->setPosition(0, y); y -= 17;
    m_info->setPosition(0, y);  y -= 14;
    for (int i = 0; i < 7; i++) {
        m_rows[i]->setVisible(tiers); m_nums[i]->setVisible(tiers);
        m_rows[i]->setColor(tierColor(i)); m_nums[i]->setColor(tierColor(i));
        m_rows[i]->setPosition(-70, y); m_nums[i]->setPosition(0, y); y -= 11;
    }
    float h = -y;
    float x = right ? win.width - 8 : 8 + 70 * sc;
    float py = top ? win.height - 6 : 6 + h * sc;
    m_box->setScale(sc);
    this->setPosition(x, py);
}

void Hud::resetCounts() {
    m_counts.fill(0);
    for (int i = 0; i < 7; i++) m_nums[i]->setString("0");
}

void Hud::setFrame(int f) { m_frame->setString(fmt::format("F {}", f).c_str()); }

void Hud::onClick(int tier, double frames, double ms, bool sub, double gap) {
    m_counts[tier]++;
    m_nums[tier]->setString(std::to_string(m_counts[tier]).c_str());
    for (auto n : {(CCNode*)m_rows[tier], (CCNode*)m_nums[tier]}) {
        n->stopAllActions(); n->setScale(0.4f);
        n->runAction(CCSequence::create(CCEaseBackOut::create(CCScaleTo::create(0.08f, 0.55f)), CCEaseSineOut::create(CCScaleTo::create(0.25f, 0.4f)), nullptr));
    }
    auto txt = sub ? fmt::format("{:.3f}f", frames) : fmt::format("{:.0f}f", frames);
    if (Mod::get()->getSettingValue<bool>("show-ms")) txt += fmt::format("  {:.3f}ms", ms);
    if (gap >= 0) txt += fmt::format("  gap {:.2f}u", gap);
    m_info->setString(txt.c_str()); m_info->setColor(tierColor(tier));
    m_info->stopAllActions(); m_info->setOpacity(255); m_info->setScale(0.7f);
    m_info->runAction(CCEaseBackOut::create(CCScaleTo::create(0.12f, 0.5f)));
    m_info->runAction(CCSequence::create(CCDelayTime::create(0.8f), CCFadeTo::create(0.5f, 0), nullptr));
}

// ------------------------------------------------------------------ rings

static std::vector<CCNode*> g_rings;

void clearRings() {
    for (auto r : g_rings) if (r->getParent()) r->removeFromParent();
    g_rings.clear();
}

void spawnRing(PlayLayer* pl, const Window& w, double fps) {
    if (!pl || !pl->m_player1 || !pl->m_objectLayer) return;
    auto mod = Mod::get();
    float sc = (float)mod->getSettingValue<double>("ring-scale");
    double life = mod->getSettingValue<double>("ring-time");              // 0 = stay until the attempt resets
    auto c3 = tierColor(w.tier);
    ccColor4F col{c3.r / 255.f, c3.g / 255.f, c3.b / 255.f, 1.f};

    auto node = CCNode::create();
    node->setPosition(pl->m_player1->getPosition());
    pl->m_objectLayer->addChild(node, 2000);
    g_rings.push_back(node);

    auto draw = CCDrawNode::create();
    const int N = 40; float R = 22.f * sc;
    CCPoint pts[N];
    for (int i = 0; i < N; i++) pts[i] = ccp(R * std::cos(i * 6.2831853f / N), R * std::sin(i * 6.2831853f / N));
    draw->drawPolygon(pts, N, {col.r, col.g, col.b, 0.12f}, 1.6f * sc, col);
    node->addChild(draw);

    if (mod->getSettingValue<bool>("ring-label")) {
        auto lbl = CCLabelBMFont::create(fmtFrames(w).c_str(), font());
        lbl->setScale(0.42f * sc); lbl->setColor(c3); lbl->setPosition({0, R + 9 * sc});
        node->addChild(lbl);
    }
    node->setScale(0.3f);
    node->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
    if (life > 0) node->runAction(CCSequence::create(CCDelayTime::create((float)life), CCFadeOut::create(0.3f), CCRemoveSelf::create(), nullptr));
}

} // namespace xfc
