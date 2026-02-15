#pragma once
#include "state/state-machine.hpp"
#include "apps/handshake/handshake-states.hpp"

class HandshakeApp : public StateMachine {
public:
    HandshakeApp(Player* player, MatchManager* matchManager,
                 QuickdrawWirelessManager* quickdrawWirelessManager);
    ~HandshakeApp() override;
    void populateStateMap() override;

    bool readyForCountdown() const;
    bool timedOut() const;

private:
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    bool handshakeComplete = false;
    bool handshakeTimedOut = false;
};
