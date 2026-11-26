#include "BotMenu.hpp"

#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_MOBILE

namespace {
    CCRect pausePanelRect(CCNode* layer) {
        CCRect best;
        float bestArea = 0.f;
        if (!layer->getChildren()) return best;
        for (auto child : CCArrayExt<CCNode*>(layer->getChildren())) {
            if (!typeinfo_cast<extension::CCScale9Sprite*>(child)) continue;
            auto box = child->boundingBox();
            float area = box.size.width * box.size.height;
            if (area > bestArea) {
                bestArea = area;
                best = box;
            }
        }
        return best;
    }
}

class $modify(BotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (!Mod::get()->getSettingValue<bool>("pause-button")) return;

        auto spr = CCSprite::create("pauseBtn.png"_spr);
        if (!spr) return;
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(BotPauseLayer::onBotMenu));
        btn->setID("easy-gdbot-button"_spr);

        auto win = CCDirector::get()->getWinSize();
        auto panel = pausePanelRect(this);
        float radius = spr->getScaledContentWidth() / 2.f;
        CCPoint pos = panel.size.width > 0.f
            ? CCPoint(panel.getMinX() + radius + 14.f, panel.getMinY() + radius + 14.f)
            : CCPoint(radius + 40.f, radius + 34.f);
        pos.x = std::clamp(pos.x, radius + 4.f, win.width - radius - 4.f);
        pos.y = std::clamp(pos.y, radius + 4.f, win.height - radius - 4.f);

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        menu->setID("easy-gdbot-menu"_spr);
        btn->setPosition(pos);
        menu->addChild(btn);
        this->addChild(menu, 10);
    }

    void onBotMenu(CCObject*) {
        BotMenu::toggle();
    }
};

#endif
