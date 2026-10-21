#include "game/player-provider.hpp"
#include "device/drivers/logger.hpp"

static const char TAG[] = "PlayerProvider";

bool PlayerProvider::hasPlayer() const { return hasProfile; }

const PlayerV2& PlayerProvider::getPlayer() const {
    if (!hasProfile) {
        LOG_E(TAG, "getPlayer called with no stored profile; returning default");
    }
    return player;
}

void PlayerProvider::setPlayer(const PlayerV2& profile) {
    player = profile;
    hasProfile = true;
}

void PlayerProvider::clear() {
    player = PlayerV2();
    hasProfile = false;
}
