#pragma once

#include "game/player.hpp"

/** Owns the session `Player` and registration PIN for fetch / gameplay handoff. */
class PlayerProvider {
public:
    static constexpr size_t REGISTRATION_PIN_BUFFER_SIZE = 5;

    /** @return the session player (never null). */
    Player* getPlayer();

    /** @return the session player (never null). */
    const Player* getPlayer() const;

    /** @return true when the owned player has a registration profile. */
    bool hasPlayer() const;

    /** Clears registration profile on the owned player and drops the registration PIN. */
    void clear();

    /** Stores the four-digit pairing PIN entered before fetch. */
    void setRegistrationPin(const char* pin);

    /** @return true when a registration PIN is stored. */
    bool hasRegistrationPin() const;

    /** @return the stored registration PIN, or an empty string when none is stored. */
    const char* getRegistrationPin() const;

    /** Seeds RNG on the owned player from the stored registration PIN. */
    void applyRngSeedFromRegistrationPin();

private:
    Player player;
    char registrationPin[REGISTRATION_PIN_BUFFER_SIZE] = {};
    bool pinStored = false;
};
