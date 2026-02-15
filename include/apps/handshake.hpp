#pragma once

#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"

constexpr int HANDSHAKE_APP_ID = 3;

class HandshakeApp : public StateMachine {
public:
    HandshakeApp(Player* player, MatchManager* matchManager,
                 QuickdrawWirelessManager* quickdrawWirelessManager);
    ~HandshakeApp();
    void populateStateMap() override;

    // External triggers for Quickdraw to check
    bool readyForCountdown();  // Handshake succeeded → start duel
    bool timedOut();           // Handshake timed out → return to idle

private:
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    bool handshakeComplete = false;
    bool handshakeTimedOut = false;
};
