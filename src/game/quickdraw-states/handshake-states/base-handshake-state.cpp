#include "game/quickdraw-states.hpp"

// Define the static member variables
SimpleTimer BaseHandshakeState::handshakeTimeout;
bool BaseHandshakeState::timeoutInitialized = false; 