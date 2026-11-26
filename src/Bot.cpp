#include "Bot.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameToolbox.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace {
    std::filesystem::path utf8Path(std::string const& s) {
        return std::filesystem::path(reinterpret_cast<char8_t const*>(s.c_str()));
    }

    std::string pathToUtf8(std::filesystem::path const& p) {
        auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string sanitizeName(std::string name) {
        std::string const bad = "\\/:*?\"<>|";
        for (auto& c : name) {
            if (bad.find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) c = '_';
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
        if (name.empty()) name = "macro";
        return name;
    }

    void fillLevelInfo(Macro& m, PlayLayer* pl) {
        m.gameVersion = GEODE_COMP_GD_VERSION;
        m.botInfo = gdr::Bot("Easy GDBot", BOT_FORMAT_VERSION);
        if (auto gm = GameManager::get()) m.author = gm->m_playerName;
        if (!pl) return;
        m.platformer = pl->m_isPlatformer;
        if (pl->m_level) {
            m.levelInfo.id = static_cast<uint32_t>(pl->m_level->m_levelID.value());
            m.levelInfo.name = pl->m_level->m_levelName;
        }
    }
}

Bot& Bot::get() {
    static Bot instance;
    return instance;
}

void Bot::notify(std::string const& text, bool error) const {
    Notification::create(text, error ? NotificationIcon::Error : NotificationIcon::Info, 1.5f)->show();
}

void Bot::loadSettings() {
    auto mod = Mod::get();
    speed = mod->getSavedValue<float>("speed", 1.f);
    speedhackAudio = mod->getSavedValue<bool>("speedhack-audio", true);
    showHitboxes = mod->getSavedValue<bool>("show-hitboxes", false);
    hitboxTrail = mod->getSavedValue<bool>("hitbox-trail", false);
    hitboxesOnDeath = mod->getSavedValue<bool>("hitboxes-on-death", false);
    noclip = mod->getSavedValue<bool>("noclip", false);
    showTrajectory = mod->getSavedValue<bool>("show-trajectory", false);
    safeMode = mod->getSavedValue<bool>("safe-mode", true);
    frameFix = mod->getSavedValue<bool>("frame-fix", true);
    checkpointFix = mod->getSavedValue<bool>("checkpoint-fix", true);
    tpsBypass = mod->getSavedValue<bool>("tps-bypass", false);
    tps = static_cast<float>(std::clamp<double>(mod->getSavedValue<float>("tps", 240.f), MIN_TPS, MAX_TPS));
    macroName = mod->getSavedValue<std::string>("macro-name", "macro");
    seedEnabled = mod->getSavedValue<bool>("seed-enabled", false);
    selectedFile = mod->getSavedValue<std::string>("selected-file", "");
    seed = mod->getSavedValue<uint64_t>("seed", 1337);
    if (seed == 0) seed = 1337;
    speedhack = false;
    frameAdvance = false;
}

void Bot::saveSettings() {
    auto mod = Mod::get();
    mod->setSavedValue<float>("speed", speed);
    mod->setSavedValue<bool>("speedhack-audio", speedhackAudio);
    mod->setSavedValue<bool>("show-hitboxes", showHitboxes);
    mod->setSavedValue<bool>("hitbox-trail", hitboxTrail);
    mod->setSavedValue<bool>("hitboxes-on-death", hitboxesOnDeath);
    mod->setSavedValue<bool>("noclip", noclip);
    mod->setSavedValue<bool>("show-trajectory", showTrajectory);
    mod->setSavedValue<bool>("safe-mode", safeMode);
    mod->setSavedValue<bool>("frame-fix", frameFix);
    mod->setSavedValue<bool>("checkpoint-fix", checkpointFix);
    mod->setSavedValue<bool>("tps-bypass", tpsBypass);
    mod->setSavedValue<float>("tps", tps);
    mod->setSavedValue<std::string>("macro-name", macroName);
    mod->setSavedValue<bool>("seed-enabled", seedEnabled);
    mod->setSavedValue<std::string>("selected-file", selectedFile);
    mod->setSavedValue<uint64_t>("seed", seed);
}

bool Bot::isCheating() const {
    return noclip
        || (speedhack && speed != 1.f)
        || currentTps() != BOT_TPS
        || frameAdvance
        || activeSeed() != 0
        || isPlaying();
}

uint64_t Bot::activeSeed() const {
    if (state == BotState::Recording || isPlaying()) return static_cast<uint32_t>(macro.seed);
    return seedEnabled ? seed : 0;
}

void Bot::applySeed(uint64_t frame) const {
    uint64_t base = activeSeed();
    if (base == 0) return;
    uint64_t z = base + frame * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    GameToolbox::fast_srand(z & 0x7fffffff);
}

void Bot::toggleSeed() {
    seedEnabled = !seedEnabled;
    saveSettings();
}

double Bot::currentTps() const {
    return tpsBypass ? static_cast<double>(tps) : BOT_TPS;
}

void Bot::setTps(float value) {
    tps = static_cast<float>(std::clamp<double>(value, MIN_TPS, MAX_TPS));
    saveSettings();
}

void Bot::toggleTpsBypass() {
    tpsBypass = !tpsBypass;
    saveSettings();
}

static void useMacroTps(Bot& bot) {
    if (std::abs(bot.macro.framerate - bot.currentTps()) < 0.001) return;
    bot.tpsBypass = std::abs(bot.macro.framerate - BOT_TPS) > 0.001;
    if (bot.tpsBypass) bot.tps = static_cast<float>(bot.macro.framerate);
    bot.saveSettings();
}

void Bot::toggleRecording() {
    if (state == BotState::Recording) {
        stop();
        return;
    }
    stop();

    auto pl = PlayLayer::get();
    beginRecording(currentTps());
    fillLevelInfo(macro, pl);
    macro.seed = seedEnabled ? static_cast<int>(static_cast<uint32_t>(seed)) : 0;

    if (pl) pl->resetLevelFromStart();
}

void Bot::togglePlayback() {
    if (state == BotState::Playing) {
        stop();
        return;
    }
    if (state == BotState::Recording) stop();
    if (macro.inputs.empty()) {
        notify("Macro is empty - record or load one", true);
        return;
    }
    armPlayback();
}

void Bot::onAttemptStart(uint64_t frame) {
    if (state != BotState::Playing) return;
    if (waitingForAttempt) {
        useMacroTps(*this);
        startArmedPlayback(frame);
    }
    else {
        seek(frame);
    }
}

void Bot::stopAll() {
    stop();
}

void Bot::clearMacro() {
    stop();
    macro = Macro();
    playIndex = 0;
}

void Bot::setSpeed(float value) {
    speed = std::clamp(value, 0.01f, 100.f);
    saveSettings();
}

void Bot::toggleSpeedhack() {
    speedhack = !speedhack;
}

void Bot::toggleNoclip() {
    noclip = !noclip;
    saveSettings();
}

void Bot::toggleTrajectory() {
    showTrajectory = !showTrajectory;
    saveSettings();
}

void Bot::toggleHitboxes() {
    showHitboxes = !showHitboxes;
    saveSettings();
}

void Bot::toggleFrameAdvance() {
    frameAdvance = !frameAdvance;
    pendingSteps = 0;
}

void Bot::stepFrame() {
    if (!frameAdvance) return;
    pendingSteps = std::min(pendingSteps + 1, 8);
}

std::filesystem::path Bot::macrosDir() const {
    auto dir = utf8Path(Mod::get()->getSaveDir().string()) / "macros";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::vector<std::string> Bot::listMacros() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(macrosDir(), ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = pathToUtf8(entry.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".gdr2") out.push_back(pathToUtf8(entry.path().filename()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

Result<> Bot::save(std::string const& rawName) {
    if (macro.inputs.empty()) return Err("Macro is empty");

    std::string name = sanitizeName(rawName);
    if (name.size() < 5 || name.substr(name.size() - 5) != ".gdr2") name += ".gdr2";

    auto pl = PlayLayer::get();
    if (macro.levelInfo.name.empty()) fillLevelInfo(macro, pl);
    if (pl) macro.duration = static_cast<float>(pl->m_gameState.m_levelTime);
    macro.sortInputs();

    auto res = macro.exportData(macrosDir() / utf8Path(name));
    if (res.isErr()) return Err(res.unwrapErr());

    macroName = name.substr(0, name.size() - 5);
    selectedFile = name;
    saveSettings();
    return Ok();
}

Result<> Bot::load(std::string const& fileName) {
    auto path = macrosDir() / utf8Path(fileName);
    auto res = Macro::importData(path);
    if (res.isErr()) return Err(res.unwrapErr());

    auto loaded = std::move(res).unwrap();
    if (!isSupportedTps(loaded.framerate)) {
        return Err(fmt::format("macro is {:.0f} TPS, supported: {:.0f}-{:.0f}", loaded.framerate, MIN_TPS, MAX_TPS));
    }

    bool ownMacro = loaded.botInfo.name == "Easy GDBot" || loaded.botInfo.name == "GDBot";
    if (ownMacro && loaded.botInfo.version < 2) {
        for (auto& input : loaded.inputs) input.player2 = !input.player2;
    }
    if (ownMacro && loaded.botInfo.version < 3) {
        notify("This macro was recorded by an older Easy GDBot - re-record it for exact timing", true);
    }

    stop();
    macro = std::move(loaded);
    macro.sortInputs();
    useMacroTps(*this);
    playIndex = 0;

    auto stem = pathToUtf8(utf8Path(fileName).stem());
    macroName = stem;
    selectedFile = fileName;
    saveSettings();
    return Ok();
}
