#pragma once

#include "game/player.hpp"

/** Owns the session `Player` shared by registration and gameplay. */
class PlayerProvider {
public:
    /** @return the session player (never null). */
    Player* getPlayer();

    /** @return the session player (never null). */
    const Player* getPlayer() const;

    /** @return true when the owned player has a registration profile. */
    bool hasPlayer() const;

    /** Clears the registration profile on the owned player. */
    void clear();

private:
    Player player;
};
