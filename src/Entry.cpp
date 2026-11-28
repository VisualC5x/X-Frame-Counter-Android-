// Entry points: the round XFC button in the main menu and in the pause menu (and nowhere else).
// Each button is made exactly as big as its neighbours in that menu, so it never looks out of place.
#include "XFC.hpp"
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {
// size (in menu units) of the first real button of `menu`, so ours matches it
float neighbourSize(CCNode* menu, float fallback) {
    if (menu && menu->getChildren()) {
        for (auto c : CCArrayExt<CCNode*>(menu->getChildren())) {
            if (!typeinfo_cast<CCMenuItemSpriteExtra*>(c)) continue;
            auto cs = c->getContentSize();
            float m = std::max(cs.width, cs.height) * std::max(0.01f, c->getScale());
            if (m >= 10.f && m <= 120.f) return m;
        }
    }
    return fallback;
}

// the round "X" icon shown at exactly `size` units
CCNode* makeBtnSprite(float size) {
    auto holder = CCNode::create();
    holder->setContentSize(CCSize{size, size});
    if (auto img = CCSprite::create("xfc-circle.png"_spr)) {
        auto cs = img->getContentSize();
        img->setScale(size / std::max(1.f, std::max(cs.width, cs.height)));
        img->setPosition(ccp(size / 2.f, size / 2.f));
        holder->addChild(img);
    } else {
        holder->addChild(ButtonSprite::create("X"));
    }
    return holder;
}
}

class $modify(XFCMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (auto menu = this->getChildByID("bottom-menu")) {
            auto btn = CCMenuItemSpriteExtra::create(makeBtnSprite(neighbourSize(menu, 50.f)), this, menu_selector(XFCMenu::onXfc));
            btn->setID("xfc-button"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            log::warn("XFC: main menu has no bottom-menu node");
        }
        return true;
    }
    void onXfc(CCObject*) { xfc::openPanel(); }
};

class $modify(XFCPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = this->getChildByID("left-button-menu");
        if (!menu) { log::warn("XFC: pause menu has no button menu node"); return; }
        auto btn = CCMenuItemSpriteExtra::create(makeBtnSprite(neighbourSize(menu, 40.f)), this, menu_selector(XFCPause::onXfc));
        btn->setID("xfc-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }
    void onXfc(CCObject*) { xfc::openPanel(); }
};

class $modify(XFCEntryPlay, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        xfc::onLevelReady(this);                            // auto start (analyse / play) after XFC opened the level
    }
};
