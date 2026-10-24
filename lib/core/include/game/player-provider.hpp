#pragma once

#include "game/player-types.hpp"

/** Owns zero or one server registration profile for an app session. */
class PlayerProvider {
public:
    static constexpr size_t REGISTRATION_PIN_BUFFER_SIZE = 5;

    /** @return true when a profile is stored. */
    bool hasPlayer() const;

    /**
     * @return the stored profile, or an empty default with `LOG_E` when none is stored.
     * Never asserts; callers should still check `hasPlayer()`.
     */
    const PlayerV2& getPlayer() const;

    /** Replaces the stored profile with @p profile. */
    void setPlayer(const PlayerV2& profile);

    /** Drops the stored profile and registration PIN. */
    void clear();

    /** Stores the four-digit pairing PIN entered before fetch. */
    void setRegistrationPin(const char* pin);

    /** @return true when a registration PIN is stored. */
    bool hasRegistrationPin() const;

    /** @return the stored registration PIN, or an empty string when none is stored. */
    const char* getRegistrationPin() const;

    /** Seeds `std::srand` from the stored registration PIN (decimal). */
    void applyRngSeedFromRegistrationPin() const;

private:
    PlayerV2 player;
    bool hasProfile = false;
    char registrationPin[REGISTRATION_PIN_BUFFER_SIZE] = {};
    bool pinStored = false;
};
