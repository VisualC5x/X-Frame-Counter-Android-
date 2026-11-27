#include "XFC.hpp"
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/UILayer.hpp>

using namespace geode::prelude;

// Width (node units) of the first real button of a menu: the XFC button copies it, so it is exactly the same size.
static float siblingSize(CCNode* menu) {
    if (!menu) return 0.f;
    auto arr = menu->getChildren();
    if (!arr) return 0.f;
    CCObject* o = nullptr;
    CCARRAY_FOREACH(arr, o) {
        auto b = typeinfo_cast<CCMenuItemSpriteExtra*>(o);
        if (!b) continue;
        float w = 0.f;
        if (auto img = b->getNormalImage()) w = img->getContentSize().width * img->getScale();
        if (w < 10.f) w = b->getContentSize().width * b->getScale();
        if (w >= 10.f) return w;
    }
    return 0.f;
}

static CCMenuItemSpriteExtra* makeButton(float width, CCObject* target, SEL_MenuHandler cb) {
    auto spr = CCSprite::create("xfc-circle.png"_spr);
    spr->setScale(width / spr->getContentSize().width);
    return CCMenuItemSpriteExtra::create(spr, target, cb);
}

// main menu: round XFC button inside the bottom row, same size as the other round buttons
class $modify(XFCMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto m = this->getChildByID("bottom-menu");
        float w = siblingSize(m); if (w <= 0.f) w = 50.f;
        auto btn = makeButton(w, this, menu_selector(XFCMenu::onXFC));
        btn->setID("xfc-button"_spr);
        if (m) { m->addChild(btn); m->updateLayout(); }
        else {
            auto win = CCDirector::get()->getWinSize();
            auto menu = CCMenu::create(); menu->setPosition({win.width - w, w}); menu->addChild(btn); this->addChild(menu);
        }
        return true;
    }
    void onXFC(CCObject*) { xfc::openPanel(); }
};

// optional small X on the play screen (off by default; the pause menu button is the main way in)
class $modify(XFCUI, UILayer) {
    bool init(GJBaseGameLayer* gl) {
        if (!UILayer::init(gl)) return false;
        if (!Mod::get()->getSettingValue<bool>("show-x-button")) return true;
        auto win = CCDirector::get()->getWinSize();
        auto spr = CCSprite::create("xfc-btn.png"_spr); spr->setScale(0.42f); spr->setOpacity(150);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(XFCUI::onXFC));
        auto menu = CCMenu::create(); menu->setPosition({win.width - 70, win.height - 24}); menu->addChild(btn); menu->setZOrder(500);
        this->addChild(menu);
        return true;
    }
    void onXFC(CCObject*) { xfc::openPanel(); }
};

// pause menu: button goes into the pause screen's own right-hand button column, same size as its buttons
class $modify(XFCPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = this->getChildByID("right-button-menu");
        float w = siblingSize(menu);
        if (w <= 0.f) w = siblingSize(this->getChildByID("left-button-menu"));
        if (w <= 0.f) w = 46.f;
        auto btn = makeButton(w, this, menu_selector(XFCPause::onXFC));
        btn->setID("xfc-button"_spr);
        if (menu) {
            menu->addChild(btn);
            menu->updateLayout();
            if (!menu->getLayout()) btn->setPosition({menu->getContentSize().width / 2, menu->getContentSize().height / 2});
        } else {
            auto win = CCDirector::get()->getWinSize();
            auto m = CCMenu::create(); m->setPosition({win.width - w, win.height / 2.f}); m->addChild(btn); this->addChild(m);
        }
    }
    void onXFC(CCObject*) { xfc::openPanel(); }
};
