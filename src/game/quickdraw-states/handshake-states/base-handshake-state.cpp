#include "game/quickdraw-states.hpp"

// Define static members of BaseHandshakeState
SimpleTimer BaseHandshakeState::handshakeTimeout;
bool BaseHandshakeState::timeoutInitialized = false; 