#pragma once

#include "game/player.hpp"
#include "game/record-key.hpp"

/** Owns the session `Player` and its record key. */
class PlayerProvider {
public:
    /** @return the session player (never null). */
    Player* getPlayer();

    /** @return the session player (never null). */
    const Player* getPlayer() const;

    /** @return true when the owned player has a registration profile. */
    bool hasPlayer() const;

    /** Clears the registration profile on the owned player and drops and overwrites the record key. */
    void clear();

    /** @return the session's record key holder (never null); registration by band fills it. */
    RecordKey* getRecordKey();

private:
    Player player;
    RecordKey recordKey;
};
