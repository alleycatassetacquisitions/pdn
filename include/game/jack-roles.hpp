#pragma once

#include "game/player.hpp"
#include "device/drivers/serial-wrapper.hpp"

// Hunter: opponent on OUTPUT_JACK, supporter on INPUT_JACK
// Bounty: opponent on INPUT_JACK, supporter on OUTPUT_JACK
inline SerialIdentifier opponentJack(const Player* p) {
    return p->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
}

inline SerialIdentifier supporterJack(const Player* p) {
    return p->isHunter() ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
}
