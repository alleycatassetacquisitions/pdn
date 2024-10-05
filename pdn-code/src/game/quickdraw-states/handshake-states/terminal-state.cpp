#include "game/handshake-machine.hpp"
//
// Created by Elli Furedy on 10/1/2024.
//
HandshakeTerminalState::HandshakeTerminalState() : State(HANDSHAKE_TERMINAL_STATE) {
}

bool HandshakeTerminalState::isTerminalState() {
    return true;
}
