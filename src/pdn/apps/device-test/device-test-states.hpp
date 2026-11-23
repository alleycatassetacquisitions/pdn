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
constexpr int POWERS_ON = 2;
constexpr int CONFIRM_POWER_FAIL = 3;
constexpr int AWAIT_OUTPUT_JACK = 4;
constexpr int REMEDIATE_INPUT_JACK = 5;
constexpr int AWAIT_SESSION_INFO = 6;
constexpr int SCREEN_CHECK = 7;
constexpr int LED_CHECK = 8;
constexpr int BUTTON_PRIMARY = 9;
constexpr int BUTTON_SECONDARY = 10;
constexpr int HAPTICS_CHECK = 11;
constexpr int AWAIT_INPUT_JACK = 12;
constexpr int POWER_SWITCH = 13;
constexpr int HOUSING = 14;
constexpr int REPORT = 15;
}  // namespace DeviceTestStateId

class DeviceTestSelectHardwareState : public TypedState<PDN> {
public:
    /** Operator selects the hardware revision under test. */
    explicit DeviceTestSelectHardwareState(DeviceTestSession* session);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestSelectHardwareState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when hardware version is chosen and id label entry follows. */
    bool transitionToEnterLabel() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToEnterLabelState = false;
};

class DeviceTestEnterLabelState : public TypedState<PDN> {
public:
    /** Operator enters the printed id label for this unit. */
    explicit DeviceTestEnterLabelState(DeviceTestSession* session);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestEnterLabelState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the label is complete and the power-on check follows. */
    bool transitionToPowersOn() const;

private:
    void showInvalidId(PDN* pdn);
    void restartDigitEntry(PDN* pdn);

    DeviceTestSession* session = nullptr;
    DigitEntry digitEntry;
    bool transitionToPowersOnState = false;
    bool showingInvalidId = false;
    SimpleTimer invalidIdTimer;
    static constexpr unsigned long INVALID_ID_DISPLAY_MS = 1500;
};

class DeviceTestPowersOnState : public TypedState<PDN> {
public:
    /** Operator confirms the DUT powers on before any jack connection. */
    explicit DeviceTestPowersOnState(DeviceTestSession* session);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestPowersOnState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the DUT powered on and output jack check follows. */
    bool transitionToAwaitOutputJack() const;
    /** @return true when power-on failed and upload confirmation follows. */
    bool transitionToConfirmPowerFail() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToAwaitOutputJackState = false;
    bool transitionToConfirmPowerFailState = false;

    static void onFail(void* ctx);
    static void onPass(void* ctx);
};

class DeviceTestConfirmPowerFailState : public TypedState<PDN> {
public:
    /** Operator confirms uploading a failed power-on inspection. */
    explicit DeviceTestConfirmPowerFailState(DeviceTestSession* session);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestConfirmPowerFailState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the operator chose to upload. */
    bool transitionToReport() const;
    /** @return true when the operator returns to the start of the inspection flow. */
    bool transitionToSelectHardware() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToReportState = false;
    bool transitionToSelectHardwareState = false;

    static void onUpload(void* ctx);
    static void onBack(void* ctx);
};

class DeviceTestAwaitOutputJackState : public ConnectState<PDN> {
public:
    /** Waits for the DUT output jack before starting the wireless session. */
    DeviceTestAwaitOutputJackState(DeviceTestSession* session,
                                   RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                   DeviceTestWirelessManager* deviceTestWirelessManager);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestAwaitOutputJackState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the output jack is connected and session info is pending. */
    bool transitionToAwaitSessionInfo() const;
    /** @return true when the operator must fix the input jack before continuing. */
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
    /** Operator reconnects the input jack when output was checked first. */
    DeviceTestRemediateInputJackState(DeviceTestSession* session,
                                      RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                      DeviceTestWirelessManager* deviceTestWirelessManager);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestRemediateInputJackState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when session info can be requested after input jack fix. */
    bool transitionToAwaitSessionInfo() const;
    /** @return true when the operator skips to power-switch check. */
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
    /** Sends begin-session and waits for DUT firmware metadata over ESP-NOW. */
    DeviceTestAwaitSessionInfoState(DeviceTestSession* session,
                                    DeviceTestWirelessManager* deviceTestWirelessManager);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestAwaitSessionInfoState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when session info arrived and component checks begin. */
    bool transitionToScreenCheck() const;
    /** @return true when begin-session fails and power-switch remediation runs. */
    bool transitionToPowerSwitch() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    bool transitionToScreenCheckState = false;
    bool transitionToPowerSwitchState = false;
    SimpleTimer beginSessionTimer;
    static constexpr unsigned long BEGIN_SESSION_INITIAL_DELAY_MS = 50;
    static constexpr unsigned long BEGIN_SESSION_RETRY_MS = 200;

    static void onFail(void* ctx);
};

class DeviceTestPromptCheckState : public TypedState<PDN> {
public:
    /** Generic PASS/FAIL prompt that drives one DUT command and records a component flag. */
    DeviceTestPromptCheckState(int stateId, DeviceTestSession* session,
                               DeviceTestWirelessManager* deviceTestWirelessManager,
                               const char* title, DeviceTestCommand dutCommand,
                               bool(DeviceTestComponents::* componentField));
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestPromptCheckState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the operator marked this component pass or fail. */
    bool transitionToNext() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    const char* titleText = "";
    DeviceTestCommand dutCommand = DeviceTestCommand::SHOW_SCREEN_PATTERN;
    bool DeviceTestComponents::* componentMember = nullptr;
    bool transitionToNextState = false;

    static void onFail(void* ctx);
    static void onPass(void* ctx);
};

class DeviceTestButtonCheckState : public TypedState<PDN> {
public:
    /** Waits for a DUT button event then records pass/fail from the operator. */
    DeviceTestButtonCheckState(int stateId, DeviceTestSession* session,
                               DeviceTestWirelessManager* deviceTestWirelessManager,
                               ButtonIdentifier expectedButton,
                               bool(DeviceTestComponents::* componentField));
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestButtonCheckState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the operator confirmed this button check. */
    bool transitionToNext() const;

private:
    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    DeviceTestOperatorPrompt prompt;
    ButtonIdentifier expectedButton = ButtonIdentifier::PRIMARY_BUTTON;
    bool DeviceTestComponents::* componentMember = nullptr;
    bool transitionToNextState = false;

    void onPacket(const uint8_t* fromMac, DeviceTestCommand command, const uint8_t* payload,
                  size_t payloadLen);
    static void onFail(void* ctx);
};

class DeviceTestAwaitInputJackState : public ConnectState<PDN> {
public:
    /** Verifies the input jack after wireless component checks. */
    DeviceTestAwaitInputJackState(DeviceTestSession* session,
                                  RemoteDeviceCoordinator* remoteDeviceCoordinator);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestAwaitInputJackState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when input jack is connected and power-switch check follows. */
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
    /** Operator verifies DUT power cycling while both jacks remain connected. */
    DeviceTestPowerSwitchState(DeviceTestSession* session,
                               RemoteDeviceCoordinator* remoteDeviceCoordinator,
                               DeviceTestWirelessManager* deviceTestWirelessManager);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestPowerSwitchState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when power switch check passes and housing check follows. */
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
    /** Operator records physical housing condition before upload. */
    explicit DeviceTestHousingState(DeviceTestSession* session);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestHousingState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when housing is marked ok or damaged and report upload runs. */
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
    /** Uploads the inspection protobuf and returns to hardware selection. */
    DeviceTestReportState(DeviceTestSession* session,
                          DeviceTestWirelessManager* deviceTestWirelessManager,
                          WirelessManager* wirelessManager);
    /** @copydoc TypedState::~TypedState */
    ~DeviceTestReportState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when upload finished and another inspection may start. */
    bool transitionToSelectHardware() const;

private:
    void renderStatus(PDN* pdn, const char* line1, const char* line2);

    DeviceTestSession* session = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    WirelessManager* wirelessManager = nullptr;
    bool transitionToSelectHardwareState = false;
    bool uploadFinished = false;
    bool uploadSucceeded = false;
    SimpleTimer uploadTimeout;
    static constexpr unsigned long UPLOAD_TIMEOUT_MS = 30000;
};
