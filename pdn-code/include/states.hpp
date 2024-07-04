#pragma once

#include <inttypes.h>

// GAME STATE MACHINE
// Qd = Quick Draw
// TODO: This will get moved and encapsulated when we switch to using
// a state machine, but it's placed here for now to facilitate having
// access from comms
enum class QdState : uint8_t {
    INITIATE = 0,
    DORMANT = 1,
    ACTIVATED = 2,
    HANDSHAKE = 3,
    DUEL_ALERT = 4,
    DUEL_COUNTDOWN = 5,
    DUEL = 6,
    WIN = 7,
    LOSE = 8,
};

extern QdState QD_STATE;