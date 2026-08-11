#pragma once

// The apps the PDN registers in its AppConfig. Each is a StateMachine the device
// mounts one at a time; cross-app edges are declared with State::addAppTransition
// and name one of these ids plus the state id to enter the target at.
constexpr int PLAYER_REGISTRATION_APP_ID = 0;
constexpr int DUEL_APP_ID = 1;
constexpr int SHOOTOUT_APP_ID = 2;
constexpr int SYMBOL_APP_ID = 3;
constexpr int HUB_APP_ID = 4;
