#pragma once

#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/device-test-wireless-manager.hpp"

class DeviceTestSubjectState : public TypedState<PDN> {
public:
    /** DUT-side passive state while the operator runs inspection on another PDN. */
    DeviceTestSubjectState(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                           DeviceTestWirelessManager* deviceTestWirelessManager);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestSubjectState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the operator ends the session and this unit returns to pin entry. */
    bool transitionToPinEntry() const;

private:
    static void onPrimaryButtonPress(void* ctx);
    static void onSecondaryButtonPress(void* ctx);

    void onDeviceTestPacket(const uint8_t* fromMac, DeviceTestCommand command,
                            const uint8_t* payload, size_t payloadLen);
    void pulseHaptics(PDN* pdn);
    void showAllLeds(PDN* pdn);
    void clearLeds(PDN* pdn);
    void showScreenPattern(PDN* pdn);

    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    SimpleTimer hapticPulseTimer;
    bool transitionToPinEntryState = false;
    bool hapticPulseActive = false;
    PDN* mountedPdn = nullptr;
    static constexpr unsigned long HAPTIC_PULSE_DURATION_MS = 500;
};
