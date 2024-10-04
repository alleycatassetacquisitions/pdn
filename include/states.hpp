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

// APPLICATION STATES
enum class AppState : uint8_t {
    DEBUG = 10,
    QD_GAME = 11,
    SET_USER = 12,
    CLEAR_USER = 13,
};

extern AppState APP_STATE;

// STATE - HANDSHAKE
/**
Handshake states:
0 - start timer to delay sending handshake signal for 1000 ms
1 - wait for handshake delay to expire
2 - start timeout timer
3 - begin sending shake message and check for timeout
**/
enum class HandshakeState : uint8_t
{
  HANDSHAKE_TIMEOUT_START_STATE = 0,
  HANDSHAKE_SEND_ROLE_SHAKE_STATE = 1,
  HANDSHAKE_WAIT_ROLE_SHAKE_STATE = 2,
  HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE = 3,
  HANDSHAKE_STATE_FINAL_ACK = 4
};

extern HandshakeState handshakeState;


// SERIAL COMMANDS
const char ENTER_DEBUG = '!';
const char START_GAME = '@';
const char SETUP_DEVICE = '#';
const char SET_ACTIVATION = '^';
const char CHECK_IN = '%';
const char DEBUG_DELIMITER = '&';
const char GET_DEVICE_ID = '*';
