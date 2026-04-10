#pragma once

#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/button.hpp"
#include "apps/fdn-connect/fdn-connect-resources.hpp"
#include "apps/fdn-connect/fdn-connect-wireless-manager.hpp"
#include "apps/fdn-connect/fdn-constants.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"

enum FDNConnectStateId {
    FDN_SEND_CONNECT   = 100,
    FDN_AWAIT_SEQUENCE = 101,
    FDN_HACKING_INPUT  = 102,
};

// Sends PDN_CONNECT to the FDN immediately on mount, then advances.
class FdnSendConnectState : public State {
public:
    FdnSendConnectState(Player* player, RemoteDeviceCoordinator* rdc, FDNConnectWirelessManager* wm);

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToAwaitSequence();

private:
    Player* player;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    FDNConnectWirelessManager* wirelessManager;
    bool sent = false;
};

// Waits for SEND_HACK_SEQUENCE from the FDN. Times out after 60 s (returning player).
class FdnAwaitSequenceState : public State {
public:
    explicit FdnAwaitSequenceState(FDNConnectWirelessManager* wm);

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToHackingInput();
    bool transitionToIdle();

    const uint8_t* getSequence() const;

private:
    FDNConnectWirelessManager* wirelessManager;
    uint8_t sequence[FDN_HACK_SEQUENCE_LENGTH] = {};
    bool sequenceReceived = false;
    bool timedOut = false;
    SimpleTimer timeoutTimer;
    static constexpr unsigned long AWAIT_TIMEOUT_MS = 60000;
};

// Handles the 8-button hacking input: lights LEDs and sends BUTTON_PRESS packets.
class FdnHackingInputState : public State {
public:
    FdnHackingInputState(FDNConnectWirelessManager* wm, RemoteDeviceCoordinator* rdc, const uint8_t* sequence);

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToIdle();

private:
    static void onPrimaryPress(void* ctx);
    static void onSecondaryPress(void* ctx);
    void handlePress(ButtonIdentifier button);
    void showCurrentPrompt();

    FDNConnectWirelessManager* wirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    const uint8_t* sequence;
    uint8_t pressCount = 0;
    bool done = false;
    Device* deviceRef = nullptr;
    SimpleTimer timeoutTimer;
    static constexpr unsigned long HACK_TIMEOUT_MS = 60000;
};
