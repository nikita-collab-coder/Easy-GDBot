#include "BotMenu.hpp"
#include "Bot.hpp"
#include "MacroImport.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
    BotMenu* s_menu = nullptr;

    constexpr float WIDTH = 440.f;
    constexpr float HEIGHT = 250.f;
    constexpr float GAP = 10.f;
    constexpr float LEFT_W = 190.f;
    constexpr float RIGHT_X = LEFT_W + GAP;
    constexpr float RIGHT_W = WIDTH - RIGHT_X;
    constexpr int TOUCH_PRIO = -600;

    bool isModifierKey(enumKeyCodes key) {
        switch (key) {
            case KEY_Shift: case KEY_Control: case KEY_Alt:
            case KEY_LeftShift: case KEY_RightShift:
            case KEY_LeftControl: case KEY_RightControl:
            case KEY_LeftMenu: case KEY_RightMenu:
            case KEY_LeftWindowsKey: case KEY_RightWindowsKey:
                return true;
            default:
                return false;
        }
    }

    CCSprite* sprite(char const* frame) {
        if (auto spr = CCSprite::createWithSpriteFrameName(frame)) return spr;
        if (auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) return spr;
        return CCSprite::create();
    }

    std::string bindText(char const* setting) {
        auto keybind = cast::typeinfo_pointer_cast<KeybindSettingV3>(Mod::get()->getSetting(setting));
        if (!keybind) return "None";
        auto const& binds = keybind->getValue();
        if (binds.empty() || binds.front().key == KEY_None) return "None";
        return binds.front().toString();
    }
}

BotMenu* BotMenu::get() {
    return s_menu;
}

void BotMenu::toggle() {
    if (s_menu) {
        s_menu->close();
        return;
    }
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto menu = new BotMenu();
    if (menu->init()) {
        menu->autorelease();
        scene->addChild(menu, 100000);
        s_menu = menu;
    }
    else {
        delete menu;
    }
}

bool BotMenu::init() {
    if (!CCLayerColor::initWithColor({0, 0, 0, 150})) return false;

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    auto win = CCDirector::get()->getWinSize();
    m_root = CCNode::create();
    m_root->setContentSize({WIDTH, HEIGHT});
    m_root->setAnchorPoint({0.5f, 0.5f});
    m_root->setPosition(win / 2);
    m_root->setScale(std::min({1.f, (win.width - 16.f) / WIDTH, (win.height - 16.f) / HEIGHT}));
    this->addChild(m_root);

#ifdef GEODE_IS_MOBILE
    m_pageCount = 3;
#else
    m_pageCount = PAGE_COUNT;
#endif

    m_binds = {
        {"menu-key",          "Menu"},
        {"record-key",        "Record"},
        {"play-key",          "Play"},
        {"speedhack-key",     "Speedhack"},
        {"hitboxes-key",      "Hitboxes"},
        {"noclip-key",        "Noclip"},
        {"trajectory-key",    "Trajectory"},
        {"frame-advance-key", "Frame Adv."},
        {"frame-step-key",    "Next Frame"},
    };

    buildMacroPanel();

    auto right = addPanel({RIGHT_X, 0, RIGHT_W, HEIGHT}, nullptr);
    m_pageTitle = addLabel(right, "Settings", {RIGHT_W / 2, HEIGHT - 20}, 0.75f, "goldFont.fnt");

    for (int i = 0; i < PAGE_COUNT; ++i) {
        auto dot = sprite("uiDot_001.png");
        dot->setScale(0.6f);
        dot->setPosition({RIGHT_W / 2 + (i - (m_pageCount - 1) / 2.f) * 12.f, HEIGHT - 38});
        dot->setVisible(i < m_pageCount);
        right->addChild(dot);
        m_pageDots[i] = dot;

        auto page = CCNode::create();
        page->setContentSize({RIGHT_W, HEIGHT});
        right->addChild(page);
        m_pages[i] = page;
    }
    buildSettingsPage(m_pages[0]);
    buildHitboxesPage(m_pages[1]);
    buildBotPage(m_pages[2]);
    buildBindsPage(m_pages[3], 0, 5);
    buildBindsPage(m_pages[4], 5, m_binds.size());

    auto arrows = CCMenu::create();
    arrows->setPosition({0, 0});
    right->addChild(arrows, 5);

    auto prevSpr = sprite("GJ_arrow_03_001.png");
    prevSpr->setScale(0.6f);
    auto prev = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(BotMenu::onPrevPage));
    prev->setPosition({-2, HEIGHT / 2});
    arrows->addChild(prev);

    auto nextSpr = sprite("GJ_arrow_03_001.png");
    nextSpr->setScale(0.6f);
    nextSpr->setFlipX(true);
    auto next = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(BotMenu::onNextPage));
    next->setPosition({RIGHT_W + 2, HEIGHT / 2});
    arrows->addChild(next);

    this->addEventListener(KeyboardInputEvent(), [this](KeyboardInputData& data) {
        if (m_capturing < 0) return false;
        if (data.action != KeyboardInputData::Action::Press) return true;
        if (isModifierKey(data.key)) return true;

        if (data.key == KEY_Escape) {
            finishBindCapture(std::nullopt);
        }
        else if (data.key == KEY_Backspace || data.key == KEY_Delete) {
            finishBindCapture(std::nullopt, true);
        }
        else {
            finishBindCapture(Keybind(data.key, data.modifiers));
        }
        return true;
    }, -1000);

    refreshFiles();
    refreshSpeedInput();
    refreshBinds();
    showPage(0);
    this->update(0.f);
    this->scheduleUpdate();
    return true;
}

void BotMenu::buildMacroPanel() {
    auto& bot = Bot::get();
    auto left = addPanel({0, 0, LEFT_W, HEIGHT}, "Macro");

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    left->addChild(menu, 5);

    auto closeSpr = sprite("GJ_closeBtn_001.png");
    closeSpr->setScale(0.6f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(BotMenu::onClose));
    closeBtn->setPosition({2, HEIGHT - 2});
    menu->addChild(closeBtn);

    m_info = addLabel(left, "", {10, HEIGHT - 38}, 0.3f, "bigFont.fnt", {0.f, 0.5f});
    m_status = addLabel(left, "", {LEFT_W - 10, HEIGHT - 38}, 0.3f, "bigFont.fnt", {1.f, 0.5f});

    float y = HEIGHT - 78;
    m_recordToggle = addCheckbox(menu, {LEFT_W * 0.28f, y}, menu_selector(BotMenu::onRecord), 0.9f);
    m_playToggle = addCheckbox(menu, {LEFT_W * 0.72f, y}, menu_selector(BotMenu::onPlay), 0.9f);
    addLabel(left, "Record", {LEFT_W * 0.28f, y - 25}, 0.4f);
    addLabel(left, "Play", {LEFT_W * 0.72f, y - 25}, 0.4f);

    y = HEIGHT - 132;
    m_nameInput = TextInput::create(LEFT_W - 20, "Macro name");
    m_nameInput->setPosition({LEFT_W / 2, y});
    m_nameInput->setScale(0.8f);
    m_nameInput->setString(bot.macroName);
    left->addChild(m_nameInput, 3);

    y -= 30;
    auto prevSpr = sprite("edit_leftBtn_001.png");
    prevSpr->setScale(0.8f);
    auto prev = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(BotMenu::onPrevFile));
    prev->setPosition({16, y});
    menu->addChild(prev);

    m_fileLabel = addLabel(left, "", {LEFT_W / 2, y}, 0.45f, "chatFont.fnt");

    auto nextSpr = sprite("edit_rightBtn_001.png");
    nextSpr->setScale(0.8f);
    auto next = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(BotMenu::onNextFile));
    next->setPosition({LEFT_W - 16, y});
    menu->addChild(next);

    y -= 32;
    addButton(menu, "Save", {LEFT_W * 0.2f, y}, menu_selector(BotMenu::onSave));
    addButton(menu, "Load", {LEFT_W * 0.5f, y}, menu_selector(BotMenu::onLoad));
    addButton(menu, "Clear", {LEFT_W * 0.8f, y}, menu_selector(BotMenu::onClear));

    auto folderSpr = sprite("gj_folderBtn_001.png");
    folderSpr->setScale(0.7f);
    auto folder = CCMenuItemSpriteExtra::create(folderSpr, this, menu_selector(BotMenu::onOpenFolder));
    folder->setPosition({24, 22});
    menu->addChild(folder);
    addLabel(left, "Open macros folder", {44, 22}, 0.3f, "bigFont.fnt", {0.f, 0.5f});
}

CCMenu* BotMenu::pageMenu(CCNode* page) {
    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    page->addChild(menu, 5);
    return menu;
}

void BotMenu::addToggleRow(CCNode* page, CCMenu* menu, char const* label, float y, std::function<bool()> get, std::function<void()> flip) {
    auto text = addLabel(page, label, {18, y}, 0.42f, "bigFont.fnt", {0.f, 0.5f});
    text->limitLabelWidth(RIGHT_W - 60.f, 0.42f, 0.2f);
    auto toggler = addCheckbox(menu, {RIGHT_W - 24, y}, menu_selector(BotMenu::onToggleRow));
    toggler->setTag(static_cast<int>(m_toggleRows.size()));
    m_toggleRows.push_back({toggler, std::move(get), std::move(flip)});
}

TextInput* BotMenu::addNumberInput(CCNode* page, char const* placeholder, float y, std::function<void(float)> onValue) {
    auto input = TextInput::create(60.f, placeholder);
    input->setPosition({RIGHT_W - 76, y});
    input->setScale(0.7f);
    input->setFilter("0123456789.");
    input->setCallback([onValue = std::move(onValue)](std::string const& value) {
        float v = 0.f;
        try { v = std::stof(value); } catch (...) { return; }
        onValue(v);
    });
    page->addChild(input, 3);
    m_pageInputs.push_back({input, 0});
    return input;
}

void BotMenu::buildSettingsPage(CCNode* page) {
    auto menu = pageMenu(page);
    auto& bot = Bot::get();
    float y = HEIGHT - 62;
    float rowH = 28;

    addToggleRow(page, menu, "TPS Bypass:", y, [&bot] { return bot.tpsBypass; }, [&bot] { bot.toggleTpsBypass(); });
    m_tpsInput = addNumberInput(page, "240", y, [this](float v) {
        m_pendingTps = v;
        m_pendingTpsTimer = 0.7f;
    });

    y -= rowH;
    addToggleRow(page, menu, "Speedhack:", y, [&bot] { return bot.speedhack; }, [&bot] { bot.toggleSpeedhack(); });
    m_speedInput = addNumberInput(page, "1.0", y, [](float v) {
        if (v > 0.f) Bot::get().setSpeed(v);
    });

    y -= rowH;
    addToggleRow(page, menu, "Speed. Audio:", y, [&bot] { return bot.speedhackAudio; },
        [&bot] { bot.speedhackAudio = !bot.speedhackAudio; bot.saveSettings(); });
    y -= rowH;
    addToggleRow(page, menu, "Noclip:", y, [&bot] { return bot.noclip; }, [&bot] { bot.toggleNoclip(); });
    y -= rowH;
    addToggleRow(page, menu, "Frame Advance:", y, [&bot] { return bot.frameAdvance; }, [&bot] { bot.toggleFrameAdvance(); });
    y -= rowH;
    addToggleRow(page, menu, "Safe Mode:", y, [&bot] { return bot.safeMode; },
        [&bot] { bot.safeMode = !bot.safeMode; bot.saveSettings(); });

    auto hint = addLabel(page, "TPS: 240 - 20000   |   Safe Mode: no progress saved while cheating", {RIGHT_W / 2, 14}, 0.3f);
    hint->limitLabelWidth(RIGHT_W - 20, 0.3f, 0.1f);
    hint->setOpacity(160);
}

void BotMenu::buildHitboxesPage(CCNode* page) {
    auto menu = pageMenu(page);
    auto& bot = Bot::get();
    float y = HEIGHT - 62;
    float rowH = 30;

    addToggleRow(page, menu, "Show Hitboxes:", y, [&bot] { return bot.showHitboxes; }, [&bot] { bot.toggleHitboxes(); });
    y -= rowH;
    addToggleRow(page, menu, "Hitbox Trail:", y, [&bot] { return bot.hitboxTrail; },
        [&bot] { bot.hitboxTrail = !bot.hitboxTrail; bot.saveSettings(); });
    y -= rowH;
    addToggleRow(page, menu, "Only On Death:", y, [&bot] { return bot.hitboxesOnDeath; },
        [&bot] { bot.hitboxesOnDeath = !bot.hitboxesOnDeath; bot.saveSettings(); });
    y -= rowH;
    addToggleRow(page, menu, "Show Trajectory (Beta):", y, [&bot] { return bot.showTrajectory; }, [&bot] { bot.toggleTrajectory(); });

    auto hint = addLabel(page, "Same hitboxes as in practice mode", {RIGHT_W / 2, 14}, 0.3f);
    hint->limitLabelWidth(RIGHT_W - 20, 0.3f, 0.1f);
    hint->setOpacity(160);
}

void BotMenu::buildBotPage(CCNode* page) {
    auto menu = pageMenu(page);
    auto& bot = Bot::get();
    float y = HEIGHT - 62;
    float rowH = 30;

    addToggleRow(page, menu, "Frame Fix:", y, [&bot] { return bot.frameFix; },
        [&bot] { bot.frameFix = !bot.frameFix; bot.saveSettings(); });
    y -= rowH;
    addToggleRow(page, menu, "Checkpoint Fix:", y, [&bot] { return bot.checkpointFix; },
        [&bot] { bot.checkpointFix = !bot.checkpointFix; bot.saveSettings(); });

    y -= rowH;
    addToggleRow(page, menu, "Seed:", y, [&bot] { return bot.seedEnabled; }, [&bot] { bot.toggleSeed(); });
    m_seedInput = TextInput::create(90.f, "1337");
    m_seedInput->setPosition({RIGHT_W - 84, y});
    m_seedInput->setScale(0.7f);
    m_seedInput->setFilter("0123456789");
    m_seedInput->setString(fmt::format("{}", bot.seed));
    m_seedInput->setCallback([](std::string const& value) {
        uint64_t v = 0;
        try { v = std::stoull(value); } catch (...) { return; }
        if (v == 0 || v > 0xFFFFFFFFull) return;
        Bot::get().seed = v;
        Bot::get().saveSettings();
    });
    page->addChild(m_seedInput, 3);
    m_pageInputs.push_back({m_seedInput, 2});

    y -= rowH - 2;
    char const* lines[] = {
        "Frame Fix: the player is put exactly where it was.",
        "Checkpoint Fix: exact state after a respawn.",
        "Seed: random triggers give the same result",
        "every attempt (macros keep their seed).",
    };
    for (auto line : lines) {
        auto l = addLabel(page, line, {18, y}, 0.5f, "chatFont.fnt", {0.f, 0.5f});
        l->setOpacity(190);
        y -= 15;
    }

    addButton(menu, "Import Mega Hack", {RIGHT_W / 2, 26}, menu_selector(BotMenu::onImportMegaHack), 0.55f);
}

void BotMenu::buildBindsPage(CCNode* page, size_t from, size_t to) {
    auto menu = pageMenu(page);

    float y = HEIGHT - 62;
    for (size_t i = from; i < to && i < m_binds.size(); ++i) {
        auto& row = m_binds[i];
        addLabel(page, (std::string(row.label) + ":").c_str(), {18, y}, 0.42f, "bigFont.fnt", {0.f, 0.5f});

        row.sprite = ButtonSprite::create("None", 90, 90, 0.5f, true, "goldFont.fnt", "GJ_button_04.png", 24.f);
        row.button = CCMenuItemSpriteExtra::create(row.sprite, this, menu_selector(BotMenu::onBind));
        row.button->setTag(static_cast<int>(i));
        row.button->setPosition({RIGHT_W - 62, y});
        menu->addChild(row.button);
        y -= 30;
    }

    auto hint = addLabel(page, "Click a key, then press the new one", {RIGHT_W / 2, 16}, 0.3f);
    hint->setOpacity(160);
    m_bindHints.push_back(hint);
}

CCNode* BotMenu::addPanel(CCRect const& rect, char const* title) {
    CCPoint origin = rect.origin;

    auto bg = extension::CCScale9Sprite::create("square02_001.png", {0, 0, 80, 80});
    bg->setContentSize(rect.size);
    bg->setColor({0, 0, 0});
    bg->setOpacity(120);
    bg->setAnchorPoint({0, 0});
    bg->setPosition(origin);
    m_root->addChild(bg);

    auto node = CCNode::create();
    node->setContentSize(rect.size);
    node->setPosition(origin);
    m_root->addChild(node, 1);

    if (title) addLabel(node, title, {rect.size.width / 2, rect.size.height - 20}, 0.75f, "goldFont.fnt");
    return node;
}

CCMenuItemSpriteExtra* BotMenu::addButton(CCMenu* menu, char const* text, CCPoint pos, SEL_MenuHandler cb, float scale) {
    auto spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", 0.8f);
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
    btn->setPosition(pos);
    menu->addChild(btn);
    return btn;
}

CCMenuItemToggler* BotMenu::addCheckbox(CCMenu* menu, CCPoint pos, SEL_MenuHandler cb, float scale) {
    auto toggler = CCMenuItemToggler::createWithStandardSprites(this, cb, scale);
    toggler->setPosition(pos);
    menu->addChild(toggler);
    return toggler;
}

CCLabelBMFont* BotMenu::addLabel(CCNode* parent, char const* text, CCPoint pos, float scale, char const* font, CCPoint anchor) {
    auto label = CCLabelBMFont::create(text, font);
    label->setScale(scale);
    label->setAnchorPoint(anchor);
    label->setPosition(pos);
    parent->addChild(label, 2);
    return label;
}

void BotMenu::onEnter() {
    CCLayerColor::onEnter();
    handleTouchPriorityWith(this, TOUCH_PRIO - 1, true);
}

void BotMenu::onExit() {
    if (s_menu == this) s_menu = nullptr;
    CCLayerColor::onExit();
}

void BotMenu::registerWithTouchDispatcher() {
    CCTouchDispatcher::get()->addTargetedDelegate(this, TOUCH_PRIO, true);
}

bool BotMenu::ccTouchBegan(CCTouch*, CCEvent*) {
    return true;
}

void BotMenu::keyBackClicked() {
    if (m_capturing >= 0) return;
    this->close();
}

void BotMenu::close() {
    applyPendingTps();
    if (s_menu == this) s_menu = nullptr;
    Bot::get().saveSettings();
    this->removeFromParentAndCleanup(true);
}

void BotMenu::showPage(int page) {
    static char const* titles[PAGE_COUNT] = {"Settings", "Hitboxes", "Bot", "Binds", "Binds"};
    if (m_capturing >= 0) finishBindCapture(std::nullopt);
    m_page = (page + m_pageCount) % m_pageCount;
    for (int i = 0; i < PAGE_COUNT; ++i) {
        m_pages[i]->setVisible(i == m_page);
        m_pageDots[i]->setColor(i == m_page ? ccColor3B{255, 255, 255} : ccColor3B{90, 90, 90});
    }
    for (auto const& [input, onPage] : m_pageInputs) input->setEnabled(m_page == onPage);
    m_pageTitle->setString(titles[m_page]);
}

void BotMenu::refreshFiles() {
    auto current = m_files.empty() ? Bot::get().selectedFile : m_files[m_fileIndex];
    m_files = Bot::get().listMacros();
    m_fileIndex = 0;
    for (size_t i = 0; i < m_files.size(); ++i) {
        if (m_files[i] == current) m_fileIndex = i;
    }
    m_fileLabel->setString(m_files.empty() ? "(no .gdr2 files)" : m_files[m_fileIndex].c_str());
    m_fileLabel->limitLabelWidth(LEFT_W - 60, 0.6f, 0.2f);
}

void BotMenu::refreshSpeedInput() {
    m_speedInput->setString(fmt::format("{:.2f}", Bot::get().speed));
    m_tpsInput->setString(fmt::format("{:.0f}", Bot::get().tps));
}

void BotMenu::refreshBinds() {
    for (size_t i = 0; i < m_binds.size(); ++i) {
        auto& row = m_binds[i];
        bool waiting = static_cast<int>(i) == m_capturing;
        row.sprite->setString(waiting ? "..." : bindText(row.setting).c_str());
        row.sprite->m_label->limitLabelWidth(80.f, 0.5f, 0.2f);
    }
    for (auto hint : m_bindHints) {
        hint->setString(m_capturing >= 0 ? "Press a key  |  Esc - cancel  |  Del - remove"
                                         : "Click a key, then press the new one");
    }
}

void BotMenu::finishBindCapture(std::optional<Keybind> bind, bool remove) {
    if (m_capturing < 0) return;
    auto const& row = m_binds[m_capturing];

    if (remove || bind) {
        if (auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(Mod::get()->getSetting(row.setting))) {
            setting->setValue(remove ? std::vector<Keybind>{} : std::vector<Keybind>{*bind});
        }
    }
    m_capturing = -1;
    refreshBinds();
}

void BotMenu::applyPendingTps() {
    if (m_pendingTps > 0.f && BotCore::isSupportedTps(m_pendingTps)) Bot::get().setTps(m_pendingTps);
    m_pendingTps = 0.f;
    m_pendingTpsTimer = 0.f;
}

void BotMenu::update(float dt) {
    auto& bot = Bot::get();

    if (m_pendingTps > 0.f) {
        m_pendingTpsTimer -= std::max(dt, 1.f / 60.f);
        if (m_pendingTpsTimer <= 0.f) applyPendingTps();
    }

    char const* stateText = "Idle";
    ccColor3B color = {200, 200, 200};
    switch (bot.state) {
        case BotState::Recording: stateText = "Recording"; color = {255, 90, 90}; break;
        case BotState::Playing:
            if (bot.waitingForAttempt) { stateText = "Waiting..."; color = {255, 220, 90}; }
            else { stateText = "Playing"; color = {90, 255, 120}; }
            break;
        default: break;
    }
    m_status->setString(stateText);
    m_status->setColor(color);

    m_info->setString(fmt::format("Actions: {}  Frame: {}", bot.macro.inputs.size(), bot.currentFrame).c_str());
    m_info->limitLabelWidth(LEFT_W - 80, 0.3f, 0.15f);

    auto sync = [](CCMenuItemToggler* t, bool on) { if (t->isToggled() != on) t->toggle(on); };
    sync(m_recordToggle, bot.state == BotState::Recording);
    sync(m_playToggle, bot.state == BotState::Playing);
    for (auto const& row : m_toggleRows) sync(row.toggler, row.get());

    if (m_pendingTps <= 0.f && m_shownTps != bot.tps) {
        m_shownTps = bot.tps;
        m_tpsInput->setString(fmt::format("{:.0f}", bot.tps));
    }
}

void BotMenu::onRecord(CCObject*) { Bot::get().toggleRecording(); }
void BotMenu::onPlay(CCObject*)   { Bot::get().togglePlayback(); }
void BotMenu::onClear(CCObject*)  { Bot::get().clearMacro(); }
void BotMenu::onClose(CCObject*)  { this->close(); }

void BotMenu::onSave(CCObject*) {
    auto& bot = Bot::get();
    auto name = m_nameInput->getString();
    if (name.empty()) name = bot.macro.levelInfo.name.empty() ? "macro" : bot.macro.levelInfo.name;

    if (auto res = bot.save(name); res.isErr()) {
        bot.notify("Save failed: " + res.unwrapErr(), true);
        return;
    }
    m_nameInput->setString(bot.macroName);
    bot.notify("Saved " + bot.macroName + ".gdr2");
    refreshFiles();
}

void BotMenu::onLoad(CCObject*) {
    if (m_files.empty()) return;
    auto& bot = Bot::get();
    auto const& file = m_files[m_fileIndex];

    if (auto res = bot.load(file); res.isErr()) {
        bot.notify("Load failed: " + res.unwrapErr(), true);
        return;
    }
    m_nameInput->setString(bot.macroName);
    bot.notify(fmt::format("Loaded {} ({} actions)", file, bot.macro.inputs.size()));
}

void BotMenu::onPrevFile(CCObject*) {
    if (m_files.empty()) return;
    m_fileIndex = (m_fileIndex + m_files.size() - 1) % m_files.size();
    Bot::get().selectedFile = m_files[m_fileIndex];
    refreshFiles();
}

void BotMenu::onNextFile(CCObject*) {
    if (m_files.empty()) return;
    m_fileIndex = (m_fileIndex + 1) % m_files.size();
    Bot::get().selectedFile = m_files[m_fileIndex];
    refreshFiles();
}

void BotMenu::onOpenFolder(CCObject*) {
    auto dir = Bot::get().macrosDir();
    if (!file::openFolder(Mod::get()->getSaveDir() / "macros")) {
        auto u8 = dir.u8string();
        FLAlertLayer::create("Macros folder", std::string(u8.begin(), u8.end()), "OK")->show();
    }
    refreshFiles();
}

void BotMenu::onImportMegaHack(CCObject*) {
    MacroImport::offer(true);
    refreshFiles();
}

void BotMenu::onToggleRow(CCObject* sender) {
    auto index = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
    if (index < m_toggleRows.size()) m_toggleRows[index].flip();
}

void BotMenu::onPrevPage(CCObject*) { showPage(m_page - 1); }
void BotMenu::onNextPage(CCObject*) { showPage(m_page + 1); }

void BotMenu::onBind(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    m_capturing = (m_capturing == index) ? -1 : index;
    refreshBinds();
}
