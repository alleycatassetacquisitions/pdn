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
}

void PlayerProvider::setRegistrationPin(const char* pin) {
    if (pin == nullptr) {
        registrationPin[0] = '\0';
        return;
    }
    strncpy(registrationPin, pin, REGISTRATION_PIN_BUFFER_SIZE - 1);
    registrationPin[REGISTRATION_PIN_BUFFER_SIZE - 1] = '\0';
}

const char* PlayerProvider::getRegistrationPin() const {
    if (registrationPin[0] == '\0') {
        LOG_E(TAG, "getRegistrationPin called with no stored PIN");
        return "";
    }
    return registrationPin;
}

void PlayerProvider::applyRngSeedFromRegistrationPin() {
    player.applyRngSeedFromDecimalString(getRegistrationPin());
}
