#pragma once

#include "game/player.hpp"
#include "game/record-key.hpp"

/** Owns the session `Player`, its record key and the PIN entered on PIN entry. */
class PlayerProvider {
public:
    static constexpr size_t REGISTRATION_PIN_BUFFER_SIZE = 5;

    /** @return the session player (never null). */
    Player* getPlayer();

    /** @return the session player (never null). */
    const Player* getPlayer() const;

    /** @return true when the owned player has a registration profile. */
    bool hasPlayer() const;

    /**
     * Clears registration profile on the owned player, drops the registration
     * PIN and drops and overwrites the record key.
     */
    void clear();

    /**
     * @return the session's record key holder (never null). Nothing sets it yet:
     * registration by band delivers the key.
     */
    RecordKey* getRecordKey();

    /** Stores the four-digit PIN entered on PIN entry. */
    void setRegistrationPin(const char* pin);

    /** @return the stored registration PIN, or an empty string when none is stored. */
    const char* getRegistrationPin() const;

    /** Seeds RNG on the owned player from the stored registration PIN. */
    void applyRngSeedFromRegistrationPin();

private:
    Player player;
    RecordKey recordKey;
    char registrationPin[REGISTRATION_PIN_BUFFER_SIZE] = {};
    bool pinStored = false;
};
