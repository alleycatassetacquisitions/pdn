#include "apps/handshake/handshake-states.hpp"

// Define static members of BaseHandshakeState
SimpleTimer* BaseHandshakeState::handshakeTimeout = nullptr;
bool BaseHandshakeState::timeoutInitialized = false; 