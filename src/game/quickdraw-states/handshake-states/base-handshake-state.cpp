#include "game/quickdraw-states.hpp"

// Define static members of BaseHandshakeState
SimpleTimer* BaseHandshakeState::handshakeTimeout = nullptr;
bool BaseHandshakeState::timeoutInitialized = false; 