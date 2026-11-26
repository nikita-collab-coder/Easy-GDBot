#pragma once

class PlayLayer;

namespace Trajectory {
    void update(PlayLayer* layer);
    void release();
    bool isSimulating();
    void onGhostDied();
}
