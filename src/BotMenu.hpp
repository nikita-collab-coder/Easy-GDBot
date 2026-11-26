#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class BotMenu : public cocos2d::CCLayerColor {
public:
    static void toggle();
    static BotMenu* get();

protected:
    struct BindRow {
        char const* setting;
        char const* label;
        CCMenuItemSpriteExtra* button = nullptr;
        ButtonSprite* sprite = nullptr;
    };

    bool init() override;
    void onEnter() override;
    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyBackClicked() override;
    void update(float dt) override;

    cocos2d::CCNode* addPanel(cocos2d::CCRect const& rect, char const* title);
    CCMenuItemSpriteExtra* addButton(cocos2d::CCMenu* menu, char const* text, cocos2d::CCPoint pos, cocos2d::SEL_MenuHandler cb, float scale = 0.6f);
    CCMenuItemToggler* addCheckbox(cocos2d::CCMenu* menu, cocos2d::CCPoint pos, cocos2d::SEL_MenuHandler cb, float scale = 0.7f);
    cocos2d::CCLabelBMFont* addLabel(cocos2d::CCNode* parent, char const* text, cocos2d::CCPoint pos, float scale,
        char const* font = "bigFont.fnt", cocos2d::CCPoint anchor = {0.5f, 0.5f});

    void buildMacroPanel();
    void buildSettingsPage(cocos2d::CCNode* page);
    void buildHitboxesPage(cocos2d::CCNode* page);
    void buildBotPage(cocos2d::CCNode* page);
    void buildBindsPage(cocos2d::CCNode* page, size_t from, size_t to);
    cocos2d::CCMenu* pageMenu(cocos2d::CCNode* page);
    void addToggleRow(cocos2d::CCNode* page, cocos2d::CCMenu* menu, char const* label, float y,
        std::function<bool()> get, std::function<void()> flip);
    geode::TextInput* addNumberInput(cocos2d::CCNode* page, char const* placeholder, float y, std::function<void(float)> onValue);

    void close();
    void refreshFiles();
    void refreshSpeedInput();
    void showPage(int page);
    void refreshBinds();
    void finishBindCapture(std::optional<geode::Keybind> bind, bool remove = false);

    void onRecord(cocos2d::CCObject*);
    void onPlay(cocos2d::CCObject*);
    void onClear(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onLoad(cocos2d::CCObject*);
    void onPrevFile(cocos2d::CCObject*);
    void onNextFile(cocos2d::CCObject*);
    void onOpenFolder(cocos2d::CCObject*);
    void onToggleRow(cocos2d::CCObject*);
    void onImportMegaHack(cocos2d::CCObject*);
    void onPrevPage(cocos2d::CCObject*);
    void onNextPage(cocos2d::CCObject*);
    void onBind(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);

    cocos2d::CCLabelBMFont* m_info = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_fileLabel = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    CCMenuItemToggler* m_recordToggle = nullptr;
    CCMenuItemToggler* m_playToggle = nullptr;

    cocos2d::CCLabelBMFont* m_pageTitle = nullptr;
    static constexpr int PAGE_COUNT = 5;
    int m_pageCount = PAGE_COUNT;
    cocos2d::CCNode* m_root = nullptr;
    std::array<cocos2d::CCNode*, PAGE_COUNT> m_pages{};
    std::array<cocos2d::CCSprite*, PAGE_COUNT> m_pageDots{};

    struct ToggleRow {
        CCMenuItemToggler* toggler;
        std::function<bool()> get;
        std::function<void()> flip;
    };
    std::vector<ToggleRow> m_toggleRows;
    std::vector<std::pair<geode::TextInput*, int>> m_pageInputs;
    std::vector<cocos2d::CCLabelBMFont*> m_bindHints;
    int m_page = 0;

    geode::TextInput* m_speedInput = nullptr;
    geode::TextInput* m_tpsInput = nullptr;
    geode::TextInput* m_seedInput = nullptr;
    float m_shownTps = 0.f;
    float m_pendingTps = 0.f;
    float m_pendingTpsTimer = 0.f;
    void applyPendingTps();

    std::vector<BindRow> m_binds;
    int m_capturing = -1;

    std::vector<std::string> m_files;
    size_t m_fileIndex = 0;
};
