#pragma once

#include "BotCore.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <vector>

class Bot : public BotCore {
public:
    static Bot& get();

    bool injecting = false;
    uint64_t currentFrame = 0;
    bool inProcessCommands = false;

    std::string selectedFile;

    bool frameFix = true;
    bool checkpointFix = true;

    bool speedhack = false;
    float speed = 1.f;
    bool speedhackAudio = true;

    bool tpsBypass = false;
    float tps = 240.f;
    double currentTps() const;
    void setTps(float value);

    bool frameAdvance = false;
    int pendingSteps = 0;
    void stepFrame();

    bool showHitboxes = false;
    bool hitboxTrail = false;
    bool hitboxesOnDeath = false;
    bool showTrajectory = false;
    bool noclip = false;
    bool safeMode = true;

    bool seedEnabled = false;
    uint64_t seed = 1337;
    uint64_t activeSeed() const;
    void applySeed(uint64_t frame) const;

    bool isCheating() const;

    std::string macroName = "macro";

    void loadSettings();
    void saveSettings();

    void toggleRecording();
    void togglePlayback();
    void onAttemptStart(uint64_t frame);
    void stopAll();
    void clearMacro();
    void setSpeed(float value);
    void toggleSpeedhack();
    void toggleHitboxes();
    void toggleNoclip();
    void toggleTrajectory();
    void toggleTpsBypass();
    void toggleFrameAdvance();
    void toggleSeed();

    std::filesystem::path macrosDir() const;
    std::vector<std::string> listMacros() const;
    geode::Result<> save(std::string const& name);
    geode::Result<> load(std::string const& name);

    void notify(std::string const& text, bool error = false) const;
};
