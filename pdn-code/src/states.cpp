#include "../include/states.hpp"

QdState QD_STATE = QdState::DORMANT;

AppState APP_STATE = AppState::QD_GAME;

HandshakeState handshakeState = HandshakeState::HANDSHAKE_TIMEOUT_START_STATE;