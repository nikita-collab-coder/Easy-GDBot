#pragma once

#include <gdr/gdr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

inline constexpr double BOT_TPS = 240.0;
inline constexpr double MIN_TPS = 240.0;
inline constexpr double MAX_TPS = 20000.0;

struct PlayerPhysics {
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    double xVelocity = 0.0;
    double yVelocity = 0.0;
};

struct BotInput : gdr::Input<"Phys"> {
    PlayerPhysics physics;
    bool hasPhysics = false;

    BotInput() = default;
    BotInput(uint64_t frame, uint8_t button, bool player2, bool down)
        : Input(frame, button, player2, down) {}
    BotInput(uint64_t frame, uint8_t button, bool player2, bool down, PlayerPhysics const& phys)
        : Input(frame, button, player2, down), physics(phys), hasPhysics(true) {}

    void parseExtension(binary_reader& reader) override {
        reader >> physics.x >> physics.y >> physics.rotation >> physics.xVelocity >> physics.yVelocity;
        hasPhysics = true;
    }
    void saveExtension(binary_writer& writer) const override {
        writer << physics.x << physics.y << physics.rotation << physics.xVelocity << physics.yVelocity;
    }
};

inline constexpr int BOT_FORMAT_VERSION = 3;

struct Macro : gdr::Replay<Macro, BotInput> {
    Macro() : Replay("Easy GDBot", BOT_FORMAT_VERSION) {
        framerate = BOT_TPS;
    }
};

enum class BotState { Idle, Recording, Playing };

class BotCore {
public:
    Macro macro;
    BotState state = BotState::Idle;
    size_t playIndex = 0;

    static bool isSupportedTps(double tps) {
        return tps >= MIN_TPS - 0.001 && tps <= MAX_TPS + 0.001;
    }

    void beginRecording(double tps = BOT_TPS) {
        macro.inputs.clear();
        macro.deaths.clear();
        macro.framerate = tps;
        state = BotState::Recording;
    }

    bool waitingForAttempt = false;

    void armPlayback() {
        state = BotState::Playing;
        waitingForAttempt = true;
    }

    void startArmedPlayback(uint64_t frame) {
        waitingForAttempt = false;
        seek(frame);
    }

    bool isPlaying() const { return state == BotState::Playing && !waitingForAttempt; }

    void beginPlayback(uint64_t currentFrame) {
        waitingForAttempt = false;
        state = BotState::Playing;
        seek(currentFrame);
    }

    void stop() {
        state = BotState::Idle;
        waitingForAttempt = false;
    }

    void recordInput(uint64_t frame, int button, bool player2, bool down) {
        if (state != BotState::Recording) return;
        macro.inputs.emplace_back(frame, static_cast<uint8_t>(button), player2, down);
    }
    void recordInput(uint64_t frame, int button, bool player2, bool down, PlayerPhysics const& physics) {
        if (state != BotState::Recording) return;
        macro.inputs.emplace_back(frame, static_cast<uint8_t>(button), player2, down, physics);
    }

    void onRecordingReset(uint64_t frame) {
        if (state != BotState::Recording) return;

        auto& in = macro.inputs;
        in.erase(std::remove_if(in.begin(), in.end(), [&](auto const& i) { return i.frame >= frame; }), in.end());

        std::array<std::array<bool, 4>, 2> held{};
        for (auto const& i : in) {
            if (i.button < 4) held[i.player2][i.button] = i.down;
        }
        for (int p = 0; p < 2; ++p) {
            for (int b = 1; b < 4; ++b) {
                if (held[p][b]) in.emplace_back(frame, static_cast<uint8_t>(b), p == 1, false);
            }
        }
    }

    void seek(uint64_t frame) {
        macro.sortInputs();
        auto& in = macro.inputs;
        auto it = std::lower_bound(in.begin(), in.end(), frame, [](auto const& i, uint64_t f) {
            return i.frame < f;
        });
        playIndex = static_cast<size_t>(it - in.begin());
    }

    template <class F>
    void popDue(uint64_t frame, F&& fn) {
        if (!isPlaying()) return;
        auto& in = macro.inputs;
        while (playIndex < in.size() && in[playIndex].frame <= frame) {
            fn(in[playIndex]);
            ++playIndex;
        }
    }

    bool finished() const { return playIndex >= macro.inputs.size(); }
};
