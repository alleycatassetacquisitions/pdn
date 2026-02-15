#pragma once

#include "state/state-machine.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "game/player.hpp"
#include "device/device.hpp"
#include "game/match-manager.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/quickdraw-resources.hpp"

constexpr int HANDSHAKE_APP_ID = 2;

class HandshakeApp : public StateMachine {
public:
    HandshakeApp(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager);
    ~HandshakeApp();

    void onStateMounted(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void populateStateMap() override;

    bool readyForCountdown() {
        return (currentState->getStateId() == HandshakeStateId::CONNECTION_SUCCESSFUL 
                && static_cast<ConnectionSuccessful*>(currentState)->transitionToCountdown()
            );
    }

    bool transitionToIdle() {
        return isTimedOut();
    }

protected:
    bool isTimedOut() {
        if (!timeoutInitialized) return false;
        handshakeTimeout.updateTime();
        return handshakeTimeout.expired();
    }

    void initTimeout() {
        handshakeTimeout.setTimer(timeout);
        timeoutInitialized = true;
    }
    
    void resetTimeout() {
        timeoutInitialized = false;
        handshakeTimeout.invalidate();
    }

private:
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    SimpleTimer handshakeTimeout;
    bool timeoutInitialized = false;
    const int timeout = 20000;
};
