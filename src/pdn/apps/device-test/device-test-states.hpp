#pragma once

#include "apps/device-test/device-test-session.hpp"
#include "apps/device-test/digit-entry.hpp"
#include "apps/device-test/device-test-operator-prompt.hpp"
#include "device/pdn.hpp"
#include "device/remote-device-coordinator.hpp"
#include "state/connect-state.hpp"
#include "state/state.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/device-test-wireless-manager.hpp"

namespace DeviceTestStateId {
constexpr int SELECT_HARDWARE = 0;
constexpr int ENTER_LABEL = 1;
constexpr int AWAIT_OUTPUT_JACK = 2;
constexpr int REMEDIATE_INPUT_JACK = 3;
constexpr int AWAIT_SESSION_INFO = 4;
constexpr int SCREEN_CHECK = 5;
constexpr int LED_CHECK = 6;
constexpr int BUTTON_PRIMARY = 7;
constexpr int BUTTON_SECONDARY = 8;
constexpr int HAPTICS_CHECK = 9;
constexpr int AWAIT_INPUT_JACK = 10;
constexpr int POWER_SWITCH = 11;
constexpr int HOUSING = 12;
constexpr int REPORT = 13;
}  // namespace DeviceTestStateId

class DeviceTestSelectHardwareState : public TypedState<PDN> {
public:
    explicit DeviceTestSelectHardwareState(DeviceTestSession* session);
    ~DeviceTestSelectHardwareState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToEnterLabel() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToEnterLabelState = false;
};

class DeviceTestEnterLabelState : public TypedState<PDN> {
public:
    explicit DeviceTestEnterLabelState(DeviceTestSession* session);
    ~DeviceTestEnterLabelState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToAwaitOutputJack() const;

private:
    DeviceTestSession* session = nullptr;
    DigitEntry digitEntry;
    bool transitionToAwaitOutputJackState = false;
};

class DeviceTestAwaitOutputJackState : public ConnectState<PDN> {
public:
    DeviceTestAwaitOutputJackState(DeviceTestSession* session,
                                 RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                 DeviceTestWirelessManager* deviceTestWirelessManager);
    ~DeviceTestAwaitOutputJackState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToAwaitSessionInfo() const;
    bool transitionToRemediateInputJack() const;

protected:
    bool isPrimaryRequired() override { return false; }
    bool isAuxRequired() override { return true; }

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToAwaitSessionInfoState = false;
    bool transitionToRemediateInputJackState = false;
};

class DeviceTestRemediateInputJackState : public ConnectState<PDN> {
public:
    DeviceTestRemediateInputJackState(DeviceTestSession* session,
                                    RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                    DeviceTestWirelessManager* deviceTestWirelessManager);
    ~DeviceTestRemediateInputJackState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToAwaitSessionInfo() const;
    bool transitionToPowerSwitch() const;

protected:
    bool isPrimaryRequired() override { return true; }
    bool isAuxRequired() override { return false; }

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToAwaitSessionInfoState = false;
    bool transitionToPowerSwitchState = false;
};

class DeviceTestAwaitSessionInfoState : public TypedState<PDN> {
public:
    DeviceTestAwaitSessionInfoState(DeviceTestSession* session, DeviceTestWirelessManager* deviceTestWirelessManager);
    ~DeviceTestAwaitSessionInfoState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToScreenCheck() const;
    bool transitionToPowerSwitch() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToScreenCheckState = false;
    bool transitionToPowerSwitchState = false;
    SimpleTimer beginSessionTimer;
    static constexpr unsigned long kBeginSessionInitialDelayMs = 50;
    static constexpr unsigned long kBeginSessionRetryMs = 200;

    static void onFail(void* ctx);
};

class DeviceTestPromptCheckState : public TypedState<PDN> {
public:
    DeviceTestPromptCheckState(int stateId,
                               DeviceTestSession* session,
                               DeviceTestWirelessManager* deviceTestWirelessManager,
                               const char* title,
                               DeviceTestCommand dutCommand,
                               bool (DeviceTestComponents::*componentField));
    ~DeviceTestPromptCheckState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToNext() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    const char* titleText = "";
    DeviceTestCommand dutCommand = DeviceTestCommand::SHOW_SCREEN_PATTERN;
    bool DeviceTestComponents::*componentMember = nullptr;
    bool transitionToNextState = false;

    static void onFail(void* ctx);
    static void onPass(void* ctx);
};

class DeviceTestButtonCheckState : public TypedState<PDN> {
public:
    DeviceTestButtonCheckState(int stateId,
                               DeviceTestSession* session,
                               DeviceTestWirelessManager* deviceTestWirelessManager,
                               ButtonIdentifier expectedButton,
                               bool (DeviceTestComponents::*componentField));
    ~DeviceTestButtonCheckState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToNext() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    ButtonIdentifier expectedButton = ButtonIdentifier::PRIMARY_BUTTON;
    bool DeviceTestComponents::*componentMember = nullptr;
    bool transitionToNextState = false;

    void onPacket(const uint8_t* fromMac, DeviceTestCommand command, const uint8_t* payload, size_t payloadLen);
    static void onFail(void* ctx);
};

class DeviceTestAwaitInputJackState : public ConnectState<PDN> {
public:
    DeviceTestAwaitInputJackState(DeviceTestSession* session, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~DeviceTestAwaitInputJackState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToPowerSwitch() const;

protected:
    bool isPrimaryRequired() override { return true; }
    bool isAuxRequired() override { return false; }

private:
    DeviceTestSession* session = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToPowerSwitchState = false;
};

class DeviceTestPowerSwitchState : public ConnectState<PDN> {
public:
    DeviceTestPowerSwitchState(DeviceTestSession* session,
                               RemoteDeviceCoordinator* remoteDeviceCoordinator,
                               DeviceTestWirelessManager* deviceTestWirelessManager);
    ~DeviceTestPowerSwitchState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToHousing() const;

protected:
    bool isPrimaryRequired() override { return true; }
    bool isAuxRequired() override { return true; }

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool dutWasOff = false;
    bool transitionToHousingState = false;

    static void onFail(void* ctx);
    static void onPass(void* ctx);
};

class DeviceTestHousingState : public TypedState<PDN> {
public:
    explicit DeviceTestHousingState(DeviceTestSession* session);
    ~DeviceTestHousingState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToReport() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToReportState = false;

    static void onDamaged(void* ctx);
    static void onOk(void* ctx);
};

class DeviceTestReportState : public TypedState<PDN> {
public:
    DeviceTestReportState(DeviceTestSession* session,
                          DeviceTestWirelessManager* deviceTestWirelessManager,
                          WirelessManager* wirelessManager);
    ~DeviceTestReportState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToSelectHardware() const;

private:
    void renderStatus(PDN* pdn, const char* line1, const char* line2);

    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    WirelessManager* wirelessManager = nullptr;
    bool transitionToSelectHardwareState = false;
    bool uploadFinished = false;
    bool uploadSucceeded = false;
};
