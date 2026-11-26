#include "Bot.hpp"
#include "BotMenu.hpp"

#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

namespace {
    template <class F>
    void bindKey(char const* setting, F action) {
        listenForKeybindSettingPresses(setting, [action](Keybind const&, bool down, bool repeat, double) {
            if (down && !repeat) action();
            return false;
        });
    }
}

$on_mod(Loaded) {
    Bot::get().loadSettings();

    bindKey("menu-key",      [] { BotMenu::toggle(); });
    bindKey("record-key",    [] { Bot::get().toggleRecording(); });
    bindKey("play-key",      [] { Bot::get().togglePlayback(); });
    bindKey("speedhack-key", [] { Bot::get().toggleSpeedhack(); });
    bindKey("hitboxes-key",  [] { Bot::get().toggleHitboxes(); });
    bindKey("noclip-key",    [] { Bot::get().toggleNoclip(); });
    bindKey("trajectory-key",[] { Bot::get().toggleTrajectory(); });
    bindKey("frame-advance-key", [] { Bot::get().toggleFrameAdvance(); });

    listenForKeybindSettingPresses("frame-step-key", [](Keybind const&, bool down, bool, double) {
        if (down) Bot::get().stepFrame();
        return false;
    });
}
