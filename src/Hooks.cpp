#include "Bot.hpp"
#include "Trajectory.hpp"

#include <Geode/modify/CCDrawNode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/CheckpointObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PlayerObject.hpp>

using namespace geode::prelude;

namespace {
    uint64_t tickFrame(GJBaseGameLayer* l) {
        double f = std::floor(l->m_gameState.m_levelTime * Bot::get().currentTps() + 0.25);
        return f < 0.0 ? 0 : static_cast<uint64_t>(f);
    }

    PlayerPhysics capture(PlayerObject* p) {
        PlayerPhysics s;
        s.x = p->m_position.x;
        s.y = p->m_position.y;
        s.rotation = p->getRotation();
        s.xVelocity = p->m_platformerXVelocity;
        s.yVelocity = p->m_yVelocity;
        return s;
    }

    void restore(PlayerObject* p, PlayerPhysics const& s) {
        p->m_position = CCPoint(s.x, s.y);
        p->setPosition(CCPoint(s.x, s.y));
        p->setRotation(s.rotation);
        p->m_platformerXVelocity = s.xVelocity;
        p->m_yVelocity = s.yVelocity;
    }

    bool s_frameStepping = false;

    float frameAdvanceDelta(float dt) {
        auto& bot = Bot::get();
        auto pl = PlayLayer::get();
        if (!bot.frameAdvance || !pl || pl->m_isPaused) return dt;
        if (bot.pendingSteps <= 0) return 0.f;
        --bot.pendingSteps;
        s_frameStepping = true;
        float timeWarp = pl->m_gameState.m_timeWarp > 0.f ? std::min(pl->m_gameState.m_timeWarp, 1.f) : 1.f;
        return static_cast<float>(timeWarp / bot.currentTps());
    }

    PlayerPhysics s_tickStart[2];

    constexpr ccColor4F HITBOX_RED  = {1.f, 0.f, 0.f, 1.f};
    constexpr ccColor4F HITBOX_BLUE = {0.f, 0.25f, 1.f, 1.f};
    constexpr float HITBOX_BORDER = 0.35f;

    bool s_inDebugDraw = false;
    bool s_drawingOwn = false;

    ccColor4F classicColor(ccColor4F const& c) {
        ccColor4F out = c;
        bool reddish = c.r >= 0.5f && c.b < 0.5f;
        bool bluish = c.b >= 0.5f && c.r < 0.5f && c.g < 0.6f;
        if (reddish) out = HITBOX_RED;
        else if (bluish) out = HITBOX_BLUE;
        out.a = 1.f;
        return out;
    }

    bool isHitboxNode(CCDrawNode* node, PlayLayer* pl) {
        return pl && (node == pl->m_debugDrawNode || node == pl->m_debugDrawNode2);
    }

    void drawRect(CCDrawNode* node, CCRect const& r, ccColor4F const& color) {
        CCPoint v[4] = {
            {r.getMinX(), r.getMinY()}, {r.getMinX(), r.getMaxY()},
            {r.getMaxX(), r.getMaxY()}, {r.getMaxX(), r.getMinY()},
        };
        node->drawPolygon(v, 4, {0.f, 0.f, 0.f, 0.f}, HITBOX_BORDER, color);
    }

    void drawPlayerHitbox(CCDrawNode* node, PlayerObject* p) {
        if (!p) return;
        s_drawingOwn = true;
        drawRect(node, p->GameObject::getObjectRect(), HITBOX_RED);
        drawRect(node, p->GameObject::getObjectRect(0.3f, 0.3f), HITBOX_BLUE);
        s_drawingOwn = false;
    }

    bool isPlayerPolygon(CCPoint const* verts, unsigned count, PlayLayer* pl) {
        if (count < 4) return false;
        float minX = verts[0].x, maxX = verts[0].x, minY = verts[0].y, maxY = verts[0].y;
        for (unsigned i = 1; i < count; ++i) {
            minX = std::min(minX, verts[i].x); maxX = std::max(maxX, verts[i].x);
            minY = std::min(minY, verts[i].y); maxY = std::max(maxY, verts[i].y);
        }
        CCPoint center = {(minX + maxX) / 2, (minY + maxY) / 2};
        for (auto p : {pl->m_player1, pl->m_gameState.m_isDualMode ? pl->m_player2 : nullptr}) {
            if (!p) continue;
            auto r = p->GameObject::getObjectRect();
            bool centered = std::abs(center.x - r.getMidX()) < 2.f && std::abs(center.y - r.getMidY()) < 2.f;
            bool fitsSize = (maxX - minX) <= r.size.width * 1.6f && (maxY - minY) <= r.size.height * 1.6f;
            if (centered && fitsSize) return true;
        }
        return false;
    }

    bool hitboxesVisible(PlayLayer* pl) {
        auto& bot = Bot::get();
        if (!bot.showHitboxes) return false;
        if (!bot.hitboxesOnDeath) return true;
        return pl->m_player1 && pl->m_player1->m_isDead;
    }
}

class $modify(BotCheckpoint, CheckpointObject) {
    struct Fields {
        PlayerPhysics m_p1;
        PlayerPhysics m_p2;
        bool m_saved = false;
    };
};

class $modify(BotBGL, GJBaseGameLayer) {
    bool isMainPlayLayer() {
        return PlayLayer::get() == static_cast<GJBaseGameLayer*>(this);
    }

    bool isTwoPlayer() {
        return m_levelSettings && m_levelSettings->m_twoPlayerMode;
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& bot = Bot::get();

        if (!this->isMainPlayLayer() || Trajectory::isSimulating()) {
            return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        }

        bot.currentFrame = tickFrame(this);

        bot.applySeed(bot.currentFrame);

        if (bot.isPlaying()) {
            bool twoPlayer = this->isTwoPlayer();
            bot.injecting = true;
            bot.popDue(bot.currentFrame, [&](BotInput const& input) {
                bool raw = input.player2;
                auto player = (raw || !twoPlayer) ? m_player1 : m_player2;

                if (bot.frameFix && input.hasPhysics && player) restore(player, input.physics);

                this->handleButton(input.down, input.button, raw);
            });
            bot.injecting = false;
        }

        if (m_player1) s_tickStart[0] = capture(m_player1);
        if (m_player2) s_tickStart[1] = capture(m_player2);

        bot.inProcessCommands = true;
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        bot.inProcessCommands = false;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto& bot = Bot::get();

        if (this->isMainPlayLayer() && !bot.injecting && !Trajectory::isSimulating()) {
            if (bot.isPlaying() && (down || bot.inProcessCommands)) return;

            if (bot.state == BotState::Recording) {
                bool usesPlayer2 = !isPlayer1 && this->isTwoPlayer();
                auto player = usesPlayer2 ? m_player2 : m_player1;
                if (bot.inProcessCommands) {
                    bot.recordInput(bot.currentFrame, button, isPlayer1, down, s_tickStart[usesPlayer2 ? 1 : 0]);
                }
                else if (player) {
                    bot.recordInput(tickFrame(this), button, isPlayer1, down, capture(player));
                }
                else {
                    bot.recordInput(tickFrame(this), button, isPlayer1, down);
                }
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void updateDebugDraw() {
        s_inDebugDraw = true;
        GJBaseGameLayer::updateDebugDraw();
        s_inDebugDraw = false;

        auto pl = PlayLayer::get();
        if (this->isMainPlayLayer() && m_debugDrawNode && hitboxesVisible(pl)) {
            drawPlayerHitbox(m_debugDrawNode, m_player1);
            if (m_gameState.m_isDualMode) drawPlayerHitbox(m_debugDrawNode, m_player2);
        }
    }

    void update(float dt) {
        if (!this->isMainPlayLayer()) return GJBaseGameLayer::update(dt);

        if (s_frameStepping) {
            s_frameStepping = false;
            m_extraDelta = 0.0;
        }

        double tps = Bot::get().currentTps();
        if (tps <= BOT_TPS) return GJBaseGameLayer::update(dt);

        float realWarp = m_gameState.m_timeWarp;
        float warp = realWarp > 0.f ? std::min(realWarp, 1.f) : 1.f;
        float scaled = static_cast<float>(warp * BOT_TPS / tps);
        m_gameState.m_timeWarp = scaled;
        GJBaseGameLayer::update(dt);
        if (m_gameState.m_timeWarp == scaled) m_gameState.m_timeWarp = realWarp;
    }
};

class $modify(BotDrawNode, CCDrawNode) {
    bool drawPolygon(CCPoint* verts, unsigned int count, ccColor4F const& fill, float border, ccColor4F const& borderColor, BorderAlignment align) {
        auto pl = PlayLayer::get();
        if (s_drawingOwn || !isHitboxNode(this, pl) || !hitboxesVisible(pl)) {
            return CCDrawNode::drawPolygon(verts, count, fill, border, borderColor, align);
        }
        if (s_inDebugDraw && this == pl->m_debugDrawNode && isPlayerPolygon(verts, count, pl)) return true;
        return CCDrawNode::drawPolygon(verts, count, {0.f, 0.f, 0.f, 0.f}, HITBOX_BORDER, classicColor(borderColor), align);
    }

    bool drawSegment(CCPoint const& from, CCPoint const& to, float radius, ccColor4F const& color) {
        auto pl = PlayLayer::get();
        if (s_drawingOwn || !isHitboxNode(this, pl) || !hitboxesVisible(pl)) {
            return CCDrawNode::drawSegment(from, to, radius, color);
        }
        return CCDrawNode::drawSegment(from, to, radius, classicColor(color));
    }
};

class $modify(BotPlayLayer, PlayLayer) {
    struct Fields {
        bool m_drawnByBot = false;
        bool m_forcedTestMode = false;
        bool m_originalTestMode = false;
    };

    void resetLevel() {
        PlayLayer::resetLevel();

        auto& bot = Bot::get();
        uint64_t frame = tickFrame(this);
        bot.currentFrame = frame;

        if (bot.state == BotState::Recording) {
            bot.onRecordingReset(frame);
            bot.macro.platformer = m_isPlatformer;
        }
        else if (bot.state == BotState::Playing) {
            bot.onAttemptStart(frame);
        }

        auto fields = m_fields.self();
        if (fields->m_forcedTestMode && !(bot.safeMode && bot.isCheating())) {
            m_isTestMode = fields->m_originalTestMode;
            fields->m_forcedTestMode = false;
        }
    }

    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
        if (!checkpoint || !m_player1) return;
        auto fields = static_cast<BotCheckpoint*>(checkpoint)->m_fields.self();
        fields->m_p1 = capture(m_player1);
        if (m_player2) fields->m_p2 = capture(m_player2);
        fields->m_saved = true;
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        if (!checkpoint || !Bot::get().checkpointFix) return;
        auto fields = static_cast<BotCheckpoint*>(checkpoint)->m_fields.self();
        if (!fields->m_saved) return;
        if (m_player1) restore(m_player1, fields->m_p1);
        if (m_player2 && m_gameState.m_isDualMode) restore(m_player2, fields->m_p2);
    }

    void levelComplete() {
        auto& bot = Bot::get();
        if (bot.state == BotState::Recording) {
            bot.macro.duration = static_cast<float>(m_gameState.m_levelTime);
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        auto& bot = Bot::get();
        bot.stop();
        bot.frameAdvance = false;
        Trajectory::release();
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (Trajectory::isSimulating()) {
            Trajectory::onGhostDied();
            return;
        }
        if (Bot::get().noclip && object != m_anticheatSpike) return;
        PlayLayer::destroyPlayer(player, object);
    }

    bool shouldDrawDebugHitboxes() {
        return hitboxesVisible(this) || PlayLayer::shouldDrawDebugHitboxes();
    }

    bool shouldDrawHitboxTrail() {
        return (hitboxesVisible(this) && Bot::get().hitboxTrail) || PlayLayer::shouldDrawHitboxTrail();
    }

    bool shouldRecordHitboxTrail() {
        return (Bot::get().showHitboxes && Bot::get().hitboxTrail) || PlayLayer::shouldRecordHitboxTrail();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& bot = Bot::get();
        auto fields = m_fields.self();

        if (bot.safeMode && bot.isCheating() && !m_isTestMode) {
            if (!fields->m_forcedTestMode) fields->m_originalTestMode = m_isTestMode;
            m_isTestMode = true;
            fields->m_forcedTestMode = true;
        }

        if (m_debugDrawNode) {
            if (hitboxesVisible(this)) {
                bool prev = m_isDebugDrawEnabled;
                m_isDebugDrawEnabled = true;
                m_debugDrawNode->setVisible(true);
                this->updateDebugDraw();
                m_isDebugDrawEnabled = prev;
                fields->m_drawnByBot = true;
            }
            else if (fields->m_drawnByBot) {
                fields->m_drawnByBot = false;
                if (!PlayLayer::shouldDrawDebugHitboxes()) {
                    m_debugDrawNode->clear();
                    m_debugDrawNode->setVisible(false);
                }
            }
        }

        Trajectory::update(this);
    }
};

class $modify(BotScheduler, CCScheduler) {
    void update(float dt) {
        auto& bot = Bot::get();
        float speed = bot.speedhack ? bot.speed : 1.f;

        static float s_appliedPitch = 1.f;
        float pitch = (bot.speedhack && bot.speedhackAudio) ? speed : 1.f;
        if (pitch != 1.f || pitch != s_appliedPitch) {
            if (auto engine = FMODAudioEngine::sharedEngine(); engine && engine->m_globalChannel) {
                engine->m_globalChannel->setPitch(pitch);
                s_appliedPitch = pitch;
            }
        }

        if (bot.frameAdvance) {
            CCScheduler::update(frameAdvanceDelta(dt));
            return;
        }
        CCScheduler::update(dt * speed);
    }
};
