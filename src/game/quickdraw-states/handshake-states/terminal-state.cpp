#include "game/handshake-machine.hpp"
#include "esp_log.h"

HandshakeTerminalState::HandshakeTerminalState() : State(HANDSHAKE_TERMINAL_STATE) {
    ESP_LOGI("TERMINAL_STATE", "State constructed");
}

bool HandshakeTerminalState::isTerminalState() {
    ESP_LOGI("TERMINAL_STATE", "Checking if terminal state");
    return true;
}