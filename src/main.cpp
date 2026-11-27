#include "XFC.hpp"
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/UILayer.hpp>

using namespace geode::prelude;

class $modify(XFCMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto spr = CCSprite::create("xfc-icon.png"_spr); spr->setScale(0.38f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(XFCMenu::onXFC));
        btn->setID("xfc-button"_spr);
        if (auto m = this->getChildByID("bottom-menu")) { m->addChild(btn); m->updateLayout(); }
        return true;
    }
    void onXFC(CCObject*) { xfc::openPanel(); }
};

// small round X on the play screen (like QOLMod's button): opens the XFC menu on top of the level
class $modify(XFCUI, UILayer) {
    bool init(GJBaseGameLayer* gl) {
        if (!UILayer::init(gl)) return false;
        if (!Mod::get()->getSettingValue<bool>("show-x-button")) return true;
        auto win = CCDirector::get()->getWinSize();
        auto spr = CCSprite::create("xfc-btn.png"_spr); spr->setScale(0.42f); spr->setOpacity(150);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(XFCUI::onXFC));
        auto menu = CCMenu::create(); menu->setPosition({22, win.height - 22}); menu->addChild(btn); menu->setZOrder(500);
        this->addChild(menu);
        return true;
    }
    void onXFC(CCObject*) { xfc::openPanel(); }
};

class $modify(XFCPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto win = CCDirector::get()->getWinSize();
        auto spr = CCSprite::create("xfc-btn.png"_spr); spr->setScale(0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(XFCPause::onXFC));
        auto menu = CCMenu::create(); menu->setPosition({win.width - 30, win.height / 2.f}); menu->addChild(btn); this->addChild(menu);   // right edge, vertically centred
        btn->setScale(0); btn->runAction(CCEaseBackOut::create(CCScaleTo::create(0.35f, 1.f)));
    }
    void onXFC(CCObject*) { xfc::openPanel(); }
};
