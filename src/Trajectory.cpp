#include "Trajectory.hpp"
#include "Bot.hpp"

#include <algorithm>
#include <vector>

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

using namespace geode::prelude;

namespace {
    constexpr float TRAJECTORY_SECONDS = 1.0f;
    constexpr int MAX_STEPS = 480;

    bool s_simulating = false;
    bool s_ghostDied = false;

    float s_frameDelta = 0.f;

    PlayLayer* s_owner = nullptr;
    Ref<CCDrawNode> s_drawNode;
    Ref<PlayerObject> s_ghost;

    void createFor(PlayLayer* pl) {
        Trajectory::release();
        s_owner = pl;

        s_drawNode = CCDrawNode::create();
        s_drawNode->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        pl->m_objectLayer->addChild(s_drawNode, 1000);

        s_ghost = PlayerObject::create(1, 1, pl, pl->m_objectLayer, false);
        s_ghost->setVisible(false);
        pl->m_objectLayer->addChild(s_ghost);
    }

    void copyState(PlayerObject* ghost, PlayerObject* real) {
        ghost->copyAttributes(real);
        ghost->m_gravityMod = real->m_gravityMod;
        ghost->m_isOnGround = real->m_isOnGround;
        ghost->m_isDead = false;
        ghost->m_playEffects = false;
        ghost->setVisible(false);
    }

    void simulate(PlayLayer* pl, PlayerObject* real, bool hold, ccColor4F color, float dt, int steps) {
        auto ghost = s_ghost.data();
        copyState(ghost, real);

        s_simulating = true;
        s_ghostDied = false;

        for (int i = 0; i < steps; ++i) {
            CCPoint from = ghost->getPosition();

            ghost->m_collisionLogTop->removeAllObjects();
            ghost->m_collisionLogBottom->removeAllObjects();
            ghost->m_collisionLogLeft->removeAllObjects();
            ghost->m_collisionLogRight->removeAllObjects();

            pl->checkCollisions(ghost, dt, false);
            if (s_ghostDied) break;

            if (i == 0) {
                if (hold) ghost->pushButton(PlayerButton::Jump);
                else ghost->releaseButton(PlayerButton::Jump);
            }

            ghost->update(dt);

            CCPoint to = ghost->getPosition();
            ccColor4F c = color;
            if (i >= steps - 40) c.a = (steps - i) / 40.f;
            s_drawNode->drawSegment(from, to, 0.6f, c);
        }

        if (s_ghostDied) {
            auto r = ghost->GameObject::getObjectRect();
            CCPoint verts[4] = {{r.getMinX(), r.getMinY()}, {r.getMinX(), r.getMaxY()}, {r.getMaxX(), r.getMaxY()}, {r.getMaxX(), r.getMinY()}};
            s_drawNode->drawPolygon(verts, 4, {color.r, color.g, color.b, 0.2f}, 0.5f, color);
        }

        s_simulating = false;
    }

    bool keepsForGhost(GameObjectType type) {
        switch (type) {
            case GameObjectType::Solid:
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
            case GameObjectType::Slope:
                return true;
            default:
                return false;
        }
    }
}

namespace Trajectory {

    bool isSimulating() { return s_simulating; }
    void onGhostDied() { s_ghostDied = true; }

    void release() {
        if (s_drawNode) s_drawNode->removeFromParent();
        if (s_ghost) s_ghost->removeFromParent();
        s_drawNode = nullptr;
        s_ghost = nullptr;
        s_owner = nullptr;
        s_frameDelta = 0.f;
    }

    void update(PlayLayer* pl) {
        auto& bot = Bot::get();
        if (!bot.showTrajectory) {
            if (s_drawNode) s_drawNode->clear();
            return;
        }
        if (!pl || !pl->m_objectLayer || !pl->m_player1) return;
        if (pl->m_levelEndAnimationStarted) {
            if (s_drawNode) s_drawNode->clear();
            return;
        }
        if (s_owner != pl || !s_drawNode || !s_ghost
            || s_drawNode->getParent() != pl->m_objectLayer || s_ghost->getParent() != pl->m_objectLayer) {
            createFor(pl);
        }

        s_drawNode->clear();
        s_drawNode->setVisible(true);

        double tps = bot.currentTps();
        float timeWarp = pl->m_gameState.m_timeWarp > 0.f ? std::min(pl->m_gameState.m_timeWarp, 1.f) : 1.f;
        float dt = s_frameDelta > 0.f ? s_frameDelta : static_cast<float>(60.0 * timeWarp / tps);
        int steps = std::min(MAX_STEPS, static_cast<int>(tps * TRAJECTORY_SECONDS));

        ccColor4F holdColor = {0.25f, 1.f, 0.35f, 1.f};
        ccColor4F releaseColor = {1.f, 0.3f, 0.3f, 1.f};

        PlayerObject* players[2] = {pl->m_player1, pl->m_gameState.m_isDualMode ? pl->m_player2 : nullptr};
        for (auto real : players) {
            if (!real || real->m_isDead) continue;
            simulate(pl, real, true, holdColor, dt, steps);
            simulate(pl, real, false, releaseColor, dt, steps);
        }
    }
}

class $modify(TrajectoryBGL, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* player, std::vector<GameObject*>* objects, int count, float dt) {
        if (!s_simulating || !objects) {
            return GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
        }

        std::vector<GameObject*> kept;
        int n = std::min<int>(count, static_cast<int>(objects->size()));
        kept.reserve(n);
        for (int i = 0; i < n; ++i) {
            auto obj = (*objects)[i];
            if (obj && keepsForGhost(obj->m_objectType)) kept.push_back(obj);
        }
        GJBaseGameLayer::collisionCheckObjects(player, &kept, static_cast<int>(kept.size()), dt);
    }

    bool canBeActivatedByPlayer(PlayerObject* p, EffectGameObject* o) {
        if (s_simulating) return false;
        return GJBaseGameLayer::canBeActivatedByPlayer(p, o);
    }

    void flipGravity(PlayerObject* p, bool flip, bool noEffects) {
        if (s_simulating) return;
        GJBaseGameLayer::flipGravity(p, flip, noEffects);
    }

    void bumpPlayer(PlayerObject* p, EffectGameObject* o) { if (!s_simulating) GJBaseGameLayer::bumpPlayer(p, o); }
    void playerTouchedRing(PlayerObject* p, RingObject* o) { if (!s_simulating) GJBaseGameLayer::playerTouchedRing(p, o); }
    void playerTouchedTrigger(PlayerObject* p, EffectGameObject* o) { if (!s_simulating) GJBaseGameLayer::playerTouchedTrigger(p, o); }
    void pickupItem(EffectGameObject* o) { if (!s_simulating) GJBaseGameLayer::pickupItem(o); }
    void teleportPlayer(TeleportPortalObject* o, PlayerObject* p) { if (!s_simulating) GJBaseGameLayer::teleportPlayer(o, p); }
    void playerWillSwitchMode(PlayerObject* p, GameObject* o) { if (!s_simulating) GJBaseGameLayer::playerWillSwitchMode(p, o); }
    void destroyObject(GameObject* o) { if (!s_simulating) GJBaseGameLayer::destroyObject(o); }
    void gameEventTriggered(GJGameEvent e, int material, int playerID) {
        if (!s_simulating) GJBaseGameLayer::gameEventTriggered(e, material, playerID);
    }
};

class $modify(TrajectoryPlayer, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        auto pl = PlayLayer::get();
        if (!s_simulating && pl && this == pl->m_player1 && dt > 0.f) s_frameDelta = dt;
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (!s_simulating) PlayerObject::ringJump(ring, skipCheck);
    }

    void incrementJumps() { if (!s_simulating) PlayerObject::incrementJumps(); }
    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (!s_simulating) PlayerObject::playSpiderDashEffect(from, to);
    }
    void spawnCircle() { if (!s_simulating) PlayerObject::spawnCircle(); }
};
