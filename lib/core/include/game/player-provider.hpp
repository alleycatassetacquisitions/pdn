#pragma once

#include "game/player-types.hpp"

/** Owns zero or one server registration profile for an app session. */
class PlayerProvider {
public:
    /** @return true when a profile is stored. */
    bool hasPlayer() const;

    /**
     * @return the stored profile, or an empty default with `LOG_E` when none is stored.
     * Never asserts; callers should still check `hasPlayer()`.
     */
    const PlayerV2& getPlayer() const;

    /** Replaces the stored profile with @p profile. */
    void setPlayer(const PlayerV2& profile);

    /** Drops the stored profile. */
    void clear();

private:
    PlayerV2 player;
    bool hasProfile = false;
};
