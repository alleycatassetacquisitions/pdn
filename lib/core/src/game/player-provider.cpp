#include "game/player-provider.hpp"

#include "device/drivers/logger.hpp"

#include <cstdlib>
#include <cstring>

static const char TAG[] = "PlayerProvider";

Player* PlayerProvider::getPlayer() { return &player; }

const Player* PlayerProvider::getPlayer() const { return &player; }

bool PlayerProvider::hasPlayer() const { return player.hasRegistrationProfile(); }

void PlayerProvider::clear() {
    player.clearRegistrationProfile();
    registrationPin[0] = '\0';
    pinStored = false;
}

void PlayerProvider::setRegistrationPin(const char* pin) {
    if (pin == nullptr) {
        registrationPin[0] = '\0';
        pinStored = false;
        return;
    }
    strncpy(registrationPin, pin, REGISTRATION_PIN_BUFFER_SIZE - 1);
    registrationPin[REGISTRATION_PIN_BUFFER_SIZE - 1] = '\0';
    pinStored = registrationPin[0] != '\0';
}

bool PlayerProvider::hasRegistrationPin() const { return pinStored; }

const char* PlayerProvider::getRegistrationPin() const {
    if (!pinStored) {
        LOG_E(TAG, "getRegistrationPin called with no stored PIN");
        return "";
    }
    return registrationPin;
}

void PlayerProvider::applyRngSeedFromRegistrationPin() const {
    const unsigned int seed =
    static_cast<unsigned int>(std::strtoul(getRegistrationPin(), nullptr, 10));
    std::srand(seed);
}
