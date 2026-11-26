#include "MacroImport.hpp"
#include "Bot.hpp"

#include <Geode/loader/Dirs.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/Popup.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace {
    fs::path utf8Path(std::string const& s) {
        return fs::path(reinterpret_cast<char8_t const*>(s.c_str()));
    }

    std::string utf8(fs::path const& p) {
        auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    fs::path megaHackFolder() {
        std::error_code ec;
        auto gameDir = utf8Path(dirs::getGameDir().string());
        for (auto const& entry : fs::directory_iterator(gameDir, ec)) {
            if (entry.is_directory(ec) && lower(utf8(entry.path().filename())) == "replays") return entry.path();
        }
        return {};
    }

    std::vector<fs::path> newMacros() {
        std::vector<fs::path> out;
        auto source = megaHackFolder();
        if (source.empty()) return out;

        auto target = Bot::get().macrosDir();
        std::error_code ec;
        for (auto const& entry : fs::directory_iterator(source, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (lower(utf8(entry.path().extension())) != ".gdr2") continue;
            if (fs::exists(target / entry.path().filename(), ec)) continue;
            out.push_back(entry.path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::set<std::string> offeredBefore() {
        auto list = Mod::get()->getSavedValue<std::vector<std::string>>("mh-offered", {});
        return {list.begin(), list.end()};
    }

    void rememberOffered(std::vector<fs::path> const& files) {
        auto set = offeredBefore();
        for (auto const& f : files) set.insert(utf8(f.filename()));
        Mod::get()->setSavedValue<std::vector<std::string>>("mh-offered", {set.begin(), set.end()});
    }

    void importFiles(std::vector<fs::path> const& files) {
        auto target = Bot::get().macrosDir();
        int copied = 0;
        for (auto const& f : files) {
            std::error_code ec;
            if (fs::copy_file(f, target / f.filename(), fs::copy_options::skip_existing, ec) && !ec) ++copied;
        }
        Bot::get().notify(fmt::format("Imported {} macro{}", copied, copied == 1 ? "" : "s"), copied == 0);
    }
}

namespace MacroImport {
    void offer(bool manual) {
        auto files = newMacros();

        if (!manual) {
            auto seen = offeredBefore();
            std::erase_if(files, [&](fs::path const& f) { return seen.contains(utf8(f.filename())); });
        }

        if (files.empty()) {
            if (manual) {
                Bot::get().notify(megaHackFolder().empty()
                    ? "No \"replays\" folder in the game folder"
                    : "No new .gdr2 macros in the \"replays\" folder", true);
            }
            return;
        }

        rememberOffered(files);

        createQuickPopup(
            "Import Mega Hack macros",
            fmt::format(
                "Found <cg>{}</c> new <cy>.gdr2</c> macro{} in the game's <cy>replays</c> folder "
                "(Mega Hack). Import {} into Easy GDBot?",
                files.size(), files.size() == 1 ? "" : "s", files.size() == 1 ? "it" : "them"
            ),
            "No", "Import",
            [files](FLAlertLayer*, bool import) {
                if (import) importFiles(files);
            }
        );
    }
}

class $modify(ImportMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        static bool s_checked = false;
        if (!s_checked) {
            s_checked = true;
            Loader::get()->queueInMainThread([] { MacroImport::offer(false); });
        }
        return true;
    }
};
