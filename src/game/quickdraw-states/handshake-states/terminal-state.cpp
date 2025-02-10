
#include "game/handshake-machine.hpp"

HandshakeTerminalState::HandshakeTerminalState() : State(HANDSHAKE_TERMINAL_STATE) {
}

bool HandshakeTerminalState::isTerminalState() {
    return true;
}