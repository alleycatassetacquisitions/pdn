#pragma once

#include "state/state-machine.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "game/player.hpp"
#include "device/device.hpp"
#include "device/serial-manager.hpp"

constexpr int HANDSHAKE_APP_ID = 2;

class HandshakeApp : public StateMachine {
public:
    HandshakeApp(HandshakeWirelessManager* handshakeWirelessManager, SerialIdentifier jack);
    ~HandshakeApp();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void populateStateMap() override;

private:
    void createOutputJackStateMap();
    void createInputJackStateMap();

    void resetApp(Device *PDN);

    HandshakeWirelessManager* handshakeWirelessManager;
    SerialIdentifier jack;

    SimpleTimer handshakeTimer;
    const int handshakeTimeout = 500;
};
