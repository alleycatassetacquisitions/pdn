#pragma once

#include <inttypes.h>
#include "State.hpp"

// APPLICATION STATES
enum class AppStates : uint8_t {
    DEBUG = 10,
    QD_GAME = 11,
    SET_USER = 12,
    CLEAR_USER = 13,
};

template <AppStates state>
class AppState : public State{};


// SERIAL COMMANDS
const char ENTER_DEBUG = '!';
const char START_GAME = '@';
const char SETUP_DEVICE = '#';
const char SET_ACTIVATION = '^';
const char CHECK_IN = '%';
const char DEBUG_DELIMITER = '&';
const char GET_DEVICE_ID = '*';
