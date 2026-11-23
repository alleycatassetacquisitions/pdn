#include "apps/device-test/device-test-states.hpp"

#include "alleycat-server/device-inventory-api.hpp"
#include "device/drivers/logger.hpp"
#include "id-generator.hpp"
#include "utils/simple-timer.hpp"
#include "utils/uuid-bytes.hpp"
#include "wireless/mac-functions.hpp"

#include <cstring>

namespace {

constexpr const char kDeviceTestReportLogTag[] = "DEVICE_TEST_REPORT";

void copyPeerMacFromPort(DeviceTestSession* session,
                         RemoteDeviceCoordinator* remoteDeviceCoordinator, SerialIdentifier port) {
    const uint8_t* mac = remoteDeviceCoordinator->getPeerMac(port);
    if (mac == nullptr) {
        session->peerMacValid = false;
        return;
    }
    memcpy(session->peerMac.data(), mac, 6);
    session->peerMacValid = true;
    strncpy(session->result.macAddress, MacToString(mac), sizeof(session->result.macAddress) - 1);
}

void markRemoteChecksFailed(DeviceTestSession* session) {
    session->remoteChecksEnabled = false;
    session->result.components.screen = false;
    session->result.components.leds = false;
    session->result.components.primaryButton = false;
    session->result.components.secondaryButton = false;
    session->result.components.haptics = false;
}

void logDeviceReportPayload(const DeviceTestResult& result) {
    char inspectionIdText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
    result.inspectionId.format(inspectionIdText);

    char deviceIdText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
    if (result.deviceId.isZero()) {
        strncpy(deviceIdText, "(unset)", sizeof(deviceIdText) - 1);
    } else {
        result.deviceId.format(deviceIdText);
    }

    LOG_I(
    kDeviceTestReportLogTag,
    "Report payload: idLabel=%d hw=%d mac=%s fw=%s inspectedAtMs=%lld deviceId=%s inspectionId=%s "
    "checks screen=%d leds=%d pri=%d sec=%d haptics=%d pwr=%d in=%d out=%d housing=%d powersOn=%d",
    result.idLabel, result.hardwareVersion, result.macAddress, result.firmwareCommitHash,
    static_cast<long long>(result.inspectedAtUnixMs), deviceIdText, inspectionIdText,
    result.components.screen ? 1 : 0, result.components.leds ? 1 : 0,
    result.components.primaryButton ? 1 : 0, result.components.secondaryButton ? 1 : 0,
    result.components.haptics ? 1 : 0, result.components.powerSwitch ? 1 : 0,
    result.components.inputJack ? 1 : 0, result.components.outputJack ? 1 : 0,
    result.components.housing ? 1 : 0, result.powersOn ? 1 : 0);
}

void logDeviceReportPreflight(const DeviceTestResult& result) {
    if (result.idLabel < 1 || result.idLabel > 9999) {
        LOG_W(kDeviceTestReportLogTag, "Preflight: idLabel out of range (%d)", result.idLabel);
    }
    if (result.deviceId.isZero()) {
        LOG_W(kDeviceTestReportLogTag, "Preflight: device UUID is nil");
    }
    if (result.firmwareCommitHash[0] == '\0') {
        LOG_W(kDeviceTestReportLogTag, "Preflight: firmware commit hash is empty");
    }
    if (result.macAddress[0] == '\0') {
        LOG_W(kDeviceTestReportLogTag, "Preflight: device MAC is empty");
    }
    if (result.inspectionId.isZero()) {
        LOG_W(kDeviceTestReportLogTag, "Preflight: inspection UUID is nil");
    }
    if (result.inspectedAtUnixMs <= 0) {
        LOG_W(kDeviceTestReportLogTag,
              "Preflight: inspectedAtUnixMs is %lld (clock may be unsynced)",
              static_cast<long long>(result.inspectedAtUnixMs));
    }
}

}  // namespace


DeviceTestSelectHardwareState::DeviceTestSelectHardwareState(DeviceTestSession* sessionIn)
    : TypedState<PDN>(DeviceTestStateId::SELECT_HARDWARE)
    , session(sessionIn) {}

DeviceTestSelectHardwareState::~DeviceTestSelectHardwareState() { session = nullptr; }

void DeviceTestSelectHardwareState::onStateMounted(PDN* pdn) {
    transitionToEnterLabelState = false;
    session->result.powersOn = false;
    prompt.mount(
    pdn, "HW Version", "Mk 2", "Mk 1", this,
    [](void* ctx) {
        DeviceTestSelectHardwareState* state = static_cast<DeviceTestSelectHardwareState*>(ctx);
        state->session->result.hardwareVersion = 2;
        state->transitionToEnterLabelState = true;
    },
    [](void* ctx) {
        DeviceTestSelectHardwareState* state = static_cast<DeviceTestSelectHardwareState*>(ctx);
        state->session->result.hardwareVersion = 1;
        state->transitionToEnterLabelState = true;
    });
}

void DeviceTestSelectHardwareState::onStateLoop(PDN* pdn) { (void)pdn; }

void DeviceTestSelectHardwareState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestSelectHardwareState::transitionToEnterLabel() const {
    return transitionToEnterLabelState;
}


DeviceTestEnterLabelState::DeviceTestEnterLabelState(DeviceTestSession* sessionIn)
    : TypedState<PDN>(DeviceTestStateId::ENTER_LABEL)
    , session(sessionIn) {}

DeviceTestEnterLabelState::~DeviceTestEnterLabelState() { session = nullptr; }

void DeviceTestEnterLabelState::onStateMounted(PDN* pdn) {
    transitionToPowersOnState = false;
    showingInvalidId = false;
    digitEntry.mount(pdn, "Device ID");
}

void DeviceTestEnterLabelState::showInvalidId(PDN* pdn) {
    pdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawCenteredText("Invalid ID", 32)
    ->render();
}

void DeviceTestEnterLabelState::restartDigitEntry(PDN* pdn) {
    showingInvalidId = false;
    digitEntry.mount(pdn, "Device ID");
}

void DeviceTestEnterLabelState::onStateLoop(PDN* pdn) {
    if (showingInvalidId) {
        invalidIdTimer.updateTime();
        if (invalidIdTimer.expired()) {
            restartDigitEntry(pdn);
        }
        return;
    }

    digitEntry.loop(pdn);
    if (!digitEntry.isComplete()) {
        return;
    }

    const int idLabel = digitEntry.getNumericValue();
    if (idLabel < 1 || idLabel > 9999) {
        digitEntry.dismount(pdn);
        showInvalidId(pdn);
        showingInvalidId = true;
        invalidIdTimer.setTimer(INVALID_ID_DISPLAY_MS);
        return;
    }

    session->result.idLabel = idLabel;
    char* deviceIdText = IdGenerator::getInstance().generateId();
    IdGenerator::uuidStringToBytes(deviceIdText, session->result.deviceId.bytes);
    transitionToPowersOnState = true;
}

void DeviceTestEnterLabelState::onStateDismounted(PDN* pdn) {
    digitEntry.dismount(pdn);
    showingInvalidId = false;
}

bool DeviceTestEnterLabelState::transitionToPowersOn() const { return transitionToPowersOnState; }


DeviceTestPowersOnState::DeviceTestPowersOnState(DeviceTestSession* sessionIn)
    : TypedState<PDN>(DeviceTestStateId::POWERS_ON)
    , session(sessionIn) {}

DeviceTestPowersOnState::~DeviceTestPowersOnState() { session = nullptr; }

void DeviceTestPowersOnState::onStateMounted(PDN* pdn) {
    transitionToAwaitOutputJackState = false;
    transitionToConfirmPowerFailState = false;
    prompt.mount(pdn, "Powers On", "PASS", "FAIL", this, onPass, onFail);
}

void DeviceTestPowersOnState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestPowersOnState::transitionToAwaitOutputJack() const {
    return transitionToAwaitOutputJackState;
}

bool DeviceTestPowersOnState::transitionToConfirmPowerFail() const {
    return transitionToConfirmPowerFailState;
}

void DeviceTestPowersOnState::onFail(void* ctx) {
    DeviceTestPowersOnState* state = static_cast<DeviceTestPowersOnState*>(ctx);
    state->session->result.powersOn = false;
    state->transitionToConfirmPowerFailState = true;
}

void DeviceTestPowersOnState::onPass(void* ctx) {
    DeviceTestPowersOnState* state = static_cast<DeviceTestPowersOnState*>(ctx);
    state->session->result.powersOn = true;
    state->transitionToAwaitOutputJackState = true;
}


DeviceTestConfirmPowerFailState::DeviceTestConfirmPowerFailState(DeviceTestSession* sessionIn)
    : TypedState<PDN>(DeviceTestStateId::CONFIRM_POWER_FAIL)
    , session(sessionIn) {}

DeviceTestConfirmPowerFailState::~DeviceTestConfirmPowerFailState() { session = nullptr; }

void DeviceTestConfirmPowerFailState::onStateMounted(PDN* pdn) {
    transitionToReportState = false;
    transitionToSelectHardwareState = false;
    prompt.mount(pdn, "Power failed", "Upload", "Back", this, onUpload, onBack);
}

void DeviceTestConfirmPowerFailState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestConfirmPowerFailState::transitionToReport() const { return transitionToReportState; }

bool DeviceTestConfirmPowerFailState::transitionToSelectHardware() const {
    return transitionToSelectHardwareState;
}

void DeviceTestConfirmPowerFailState::onUpload(void* ctx) {
    DeviceTestConfirmPowerFailState* state = static_cast<DeviceTestConfirmPowerFailState*>(ctx);
    state->transitionToReportState = true;
}

void DeviceTestConfirmPowerFailState::onBack(void* ctx) {
    DeviceTestConfirmPowerFailState* state = static_cast<DeviceTestConfirmPowerFailState*>(ctx);
    state->transitionToSelectHardwareState = true;
}


DeviceTestAwaitOutputJackState::DeviceTestAwaitOutputJackState(
DeviceTestSession* sessionIn, RemoteDeviceCoordinator* remoteDeviceCoordinator,
DeviceTestWirelessManager* deviceTestWirelessManagerIn)
    : ConnectState<PDN>(remoteDeviceCoordinator, DeviceTestStateId::AWAIT_OUTPUT_JACK)
    , session(sessionIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn) {}

DeviceTestAwaitOutputJackState::~DeviceTestAwaitOutputJackState() {
    session = nullptr;
    deviceTestWirelessManager = nullptr;
}

void DeviceTestAwaitOutputJackState::onStateMounted(PDN* pdn) {
    transitionToAwaitSessionInfoState = false;
    transitionToRemediateInputJackState = false;
    session->result.components.outputJack = false;
    prompt.mount(
    pdn, "Output Jack", "WAIT", "FAIL", this, [](void* ctx) { (void)ctx; },
    [](void* ctx) {
        DeviceTestAwaitOutputJackState* state = static_cast<DeviceTestAwaitOutputJackState*>(ctx);
        state->session->result.components.outputJack = false;
        state->transitionToRemediateInputJackState = true;
    });
}

void DeviceTestAwaitOutputJackState::onStateLoop(PDN* pdn) {
    if (transitionToRemediateInputJackState || transitionToAwaitSessionInfoState) {
        return;
    }
    if (isConnected()) {
        copyPeerMacFromPort(session, remoteDeviceCoordinator, SerialIdentifier::INPUT_JACK);
        if (session->peerMacValid) {
            session->result.components.outputJack = true;
            transitionToAwaitSessionInfoState = true;
        }
    }
}

void DeviceTestAwaitOutputJackState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestAwaitOutputJackState::transitionToAwaitSessionInfo() const {
    return transitionToAwaitSessionInfoState;
}

bool DeviceTestAwaitOutputJackState::transitionToRemediateInputJack() const {
    return transitionToRemediateInputJackState;
}


DeviceTestRemediateInputJackState::DeviceTestRemediateInputJackState(
DeviceTestSession* sessionIn, RemoteDeviceCoordinator* remoteDeviceCoordinator,
DeviceTestWirelessManager* deviceTestWirelessManagerIn)
    : ConnectState<PDN>(remoteDeviceCoordinator, DeviceTestStateId::REMEDIATE_INPUT_JACK)
    , session(sessionIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn) {}

DeviceTestRemediateInputJackState::~DeviceTestRemediateInputJackState() {
    session = nullptr;
    deviceTestWirelessManager = nullptr;
}

void DeviceTestRemediateInputJackState::onStateMounted(PDN* pdn) {
    transitionToAwaitSessionInfoState = false;
    transitionToPowerSwitchState = false;
    session->inputJackTested = false;
    prompt.mount(
    pdn, "Add Input Jack", "WAIT", "FAIL", this, [](void* ctx) { (void)ctx; },
    [](void* ctx) {
        DeviceTestRemediateInputJackState* state =
        static_cast<DeviceTestRemediateInputJackState*>(ctx);
        state->session->result.components.inputJack = false;
        state->session->inputJackTested = true;
        markRemoteChecksFailed(state->session);
        state->transitionToPowerSwitchState = true;
    });
}

void DeviceTestRemediateInputJackState::onStateLoop(PDN* pdn) {
    (void)pdn;
    if (transitionToPowerSwitchState || transitionToAwaitSessionInfoState) {
        return;
    }
    if (isConnected()) {
        copyPeerMacFromPort(session, remoteDeviceCoordinator, SerialIdentifier::OUTPUT_JACK);
        session->result.components.inputJack = true;
        session->inputJackTested = true;
        session->remoteChecksEnabled = true;
        transitionToAwaitSessionInfoState = true;
    }
}

void DeviceTestRemediateInputJackState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestRemediateInputJackState::transitionToAwaitSessionInfo() const {
    return transitionToAwaitSessionInfoState;
}

bool DeviceTestRemediateInputJackState::transitionToPowerSwitch() const {
    return transitionToPowerSwitchState;
}


DeviceTestAwaitSessionInfoState::DeviceTestAwaitSessionInfoState(
DeviceTestSession* sessionIn, DeviceTestWirelessManager* deviceTestWirelessManagerIn)
    : TypedState<PDN>(DeviceTestStateId::AWAIT_SESSION_INFO)
    , session(sessionIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn) {}

DeviceTestAwaitSessionInfoState::~DeviceTestAwaitSessionInfoState() {
    session = nullptr;
    deviceTestWirelessManager = nullptr;
}

void DeviceTestAwaitSessionInfoState::onStateMounted(PDN* pdn) {
    transitionToScreenCheckState = false;
    transitionToPowerSwitchState = false;
    beginSessionTimer.setTimer(BEGIN_SESSION_INITIAL_DELAY_MS);

    prompt.mount(pdn, "Beginning...", "WAIT", "FAIL", this, [](void* ctx) { (void)ctx; }, onFail);

    deviceTestWirelessManager->setPeerFilter([this](const uint8_t* mac) {
        if (!session->peerMacValid) {
            return false;
        }
        return memcmp(mac, session->peerMac.data(), 6) == 0;
    });

    deviceTestWirelessManager->setPacketReceivedCallback(
    [this](const uint8_t* fromMac, DeviceTestCommand command, const uint8_t* payload,
           size_t payloadLen) {
        (void)fromMac;
        if (command != DeviceTestCommand::SESSION_INFO) {
            return;
        }
        if (payloadLen >= 8) {
            memcpy(session->result.firmwareCommitHash, payload, 8);
            session->result.firmwareCommitHash[8] = '\0';
        }
        transitionToScreenCheckState = true;
    });
}

void DeviceTestAwaitSessionInfoState::onStateLoop(PDN* pdn) {
    (void)pdn;
    if (transitionToScreenCheckState) {
        return;
    }
    if (!session->remoteChecksEnabled) {
        transitionToPowerSwitchState = true;
        return;
    }
    if (session->peerMacValid && beginSessionTimer.expired()) {
        deviceTestWirelessManager->sendBeginSession(session->peerMac.data());
        beginSessionTimer.setTimer(BEGIN_SESSION_RETRY_MS);
    }
}

void DeviceTestAwaitSessionInfoState::onStateDismounted(PDN* pdn) {
    prompt.dismount(pdn);
    deviceTestWirelessManager->clearCallbacks();
}

void DeviceTestAwaitSessionInfoState::onFail(void* ctx) {
    DeviceTestAwaitSessionInfoState* state = static_cast<DeviceTestAwaitSessionInfoState*>(ctx);
    markRemoteChecksFailed(state->session);
    state->transitionToPowerSwitchState = true;
}

bool DeviceTestAwaitSessionInfoState::transitionToScreenCheck() const {
    return transitionToScreenCheckState;
}

bool DeviceTestAwaitSessionInfoState::transitionToPowerSwitch() const {
    return transitionToPowerSwitchState;
}


DeviceTestPromptCheckState::DeviceTestPromptCheckState(
int stateId, DeviceTestSession* sessionIn, DeviceTestWirelessManager* deviceTestWirelessManagerIn,
const char* title, DeviceTestCommand command, bool DeviceTestComponents::* componentField)
    : TypedState<PDN>(stateId)
    , session(sessionIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn)
    , titleText(title)
    , dutCommand(command)
    , componentMember(componentField) {}

DeviceTestPromptCheckState::~DeviceTestPromptCheckState() {
    session = nullptr;
    deviceTestWirelessManager = nullptr;
}

void DeviceTestPromptCheckState::onStateMounted(PDN* pdn) {
    transitionToNextState = false;
    if (!session->remoteChecksEnabled) {
        session->result.components.*componentMember = false;
        transitionToNextState = true;
        return;
    }

    if (session->peerMacValid) {
        switch (dutCommand) {
            case DeviceTestCommand::SHOW_SCREEN_PATTERN:
                deviceTestWirelessManager->sendShowScreenPattern(session->peerMac.data());
                break;
            case DeviceTestCommand::SHOW_ALL_LEDS:
                deviceTestWirelessManager->sendShowAllLeds(session->peerMac.data());
                break;
            case DeviceTestCommand::TRIGGER_HAPTICS:
                deviceTestWirelessManager->sendTriggerHaptics(session->peerMac.data());
                break;
            default: break;
        }
    }

    prompt.mount(pdn, titleText, "PASS", "FAIL", this, onPass, onFail);
}

void DeviceTestPromptCheckState::onStateDismounted(PDN* pdn) {
    (void)pdn;
    if (session->remoteChecksEnabled && session->peerMacValid &&
        dutCommand == DeviceTestCommand::SHOW_ALL_LEDS) {
        deviceTestWirelessManager->sendClearLeds(session->peerMac.data());
    }
    prompt.dismount(pdn);
}

bool DeviceTestPromptCheckState::transitionToNext() const { return transitionToNextState; }

void DeviceTestPromptCheckState::onFail(void* ctx) {
    DeviceTestPromptCheckState* state = static_cast<DeviceTestPromptCheckState*>(ctx);
    state->session->result.components.*state->componentMember = false;
    state->transitionToNextState = true;
}

void DeviceTestPromptCheckState::onPass(void* ctx) {
    DeviceTestPromptCheckState* state = static_cast<DeviceTestPromptCheckState*>(ctx);
    state->session->result.components.*state->componentMember = true;
    state->transitionToNextState = true;
}


DeviceTestButtonCheckState::DeviceTestButtonCheckState(
int stateId, DeviceTestSession* sessionIn, DeviceTestWirelessManager* deviceTestWirelessManagerIn,
ButtonIdentifier expected, bool DeviceTestComponents::* componentField)
    : TypedState<PDN>(stateId)
    , session(sessionIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn)
    , expectedButton(expected)
    , componentMember(componentField) {}

DeviceTestButtonCheckState::~DeviceTestButtonCheckState() {
    session = nullptr;
    deviceTestWirelessManager = nullptr;
}

void DeviceTestButtonCheckState::onStateMounted(PDN* pdn) {
    transitionToNextState = false;
    if (!session->remoteChecksEnabled) {
        session->result.components.*componentMember = false;
        transitionToNextState = true;
        return;
    }

    const char* title =
    expectedButton == ButtonIdentifier::PRIMARY_BUTTON ? "Top Button" : "Bottom Button";
    prompt.mount(pdn, title, "WAIT", "FAIL", this, [](void* ctx) { (void)ctx; }, onFail);

    deviceTestWirelessManager->setPeerFilter([this](const uint8_t* mac) {
        return session->peerMacValid && memcmp(mac, session->peerMac.data(), 6) == 0;
    });
    deviceTestWirelessManager->setPacketReceivedCallback(
    [this](const uint8_t* fromMac, DeviceTestCommand command, const uint8_t* payload,
           size_t payloadLen) { onPacket(fromMac, command, payload, payloadLen); });
}

void DeviceTestButtonCheckState::onStateDismounted(PDN* pdn) {
    prompt.dismount(pdn);
    deviceTestWirelessManager->clearCallbacks();
}

bool DeviceTestButtonCheckState::transitionToNext() const { return transitionToNextState; }

void DeviceTestButtonCheckState::onPacket(const uint8_t* fromMac, DeviceTestCommand command,
                                          const uint8_t* payload, size_t payloadLen) {
    (void)fromMac;
    if (command != DeviceTestCommand::BUTTON_EVENT || payloadLen < 1) {
        return;
    }
    const ButtonIdentifier reported = static_cast<ButtonIdentifier>(payload[0]);
    if (reported != expectedButton) {
        return;
    }
    session->result.components.*componentMember = true;
    transitionToNextState = true;
}

void DeviceTestButtonCheckState::onFail(void* ctx) {
    DeviceTestButtonCheckState* state = static_cast<DeviceTestButtonCheckState*>(ctx);
    state->session->result.components.*state->componentMember = false;
    state->transitionToNextState = true;
}


DeviceTestAwaitInputJackState::DeviceTestAwaitInputJackState(
DeviceTestSession* sessionIn, RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState<PDN>(remoteDeviceCoordinator, DeviceTestStateId::AWAIT_INPUT_JACK)
    , session(sessionIn) {}

DeviceTestAwaitInputJackState::~DeviceTestAwaitInputJackState() { session = nullptr; }

void DeviceTestAwaitInputJackState::onStateMounted(PDN* pdn) {
    transitionToPowerSwitchState = false;
    prompt.mount(
    pdn, "Input Jack", "WAIT", "FAIL", this, [](void* ctx) { (void)ctx; },
    [](void* ctx) {
        DeviceTestAwaitInputJackState* state = static_cast<DeviceTestAwaitInputJackState*>(ctx);
        state->session->result.components.inputJack = false;
        state->session->inputJackTested = true;
        state->transitionToPowerSwitchState = true;
    });
}

void DeviceTestAwaitInputJackState::onStateLoop(PDN* pdn) {
    (void)pdn;
    if (transitionToPowerSwitchState) {
        return;
    }
    if (!session->peerMacValid) {
        return;
    }
    if (!isConnected()) {
        return;
    }
    const uint8_t* mac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
    if (mac != nullptr && memcmp(mac, session->peerMac.data(), 6) == 0) {
        session->result.components.inputJack = true;
        session->inputJackTested = true;
        transitionToPowerSwitchState = true;
    }
}

void DeviceTestAwaitInputJackState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestAwaitInputJackState::transitionToPowerSwitch() const {
    return transitionToPowerSwitchState;
}


DeviceTestPowerSwitchState::DeviceTestPowerSwitchState(
DeviceTestSession* sessionIn, RemoteDeviceCoordinator* remoteDeviceCoordinator,
DeviceTestWirelessManager* deviceTestWirelessManagerIn)
    : ConnectState<PDN>(remoteDeviceCoordinator, DeviceTestStateId::POWER_SWITCH)
    , session(sessionIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn) {}

DeviceTestPowerSwitchState::~DeviceTestPowerSwitchState() {
    session = nullptr;
    deviceTestWirelessManager = nullptr;
}

void DeviceTestPowerSwitchState::onStateMounted(PDN* pdn) {
    transitionToHousingState = false;
    dutWasOff = false;
    prompt.mount(pdn, "Power Switch", "PASS", "FAIL", this, onPass, onFail);
}

void DeviceTestPowerSwitchState::onStateLoop(PDN* pdn) {
    (void)pdn;
    if (!session->peerMacValid) {
        return;
    }
    if (!isConnected()) {
        dutWasOff = true;
        prompt.setTitle("DUT off");
        return;
    }
    if (dutWasOff) {
        prompt.setTitle("DUT back");
        deviceTestWirelessManager->sendBeginSession(session->peerMac.data());
        dutWasOff = false;
    }
}

void DeviceTestPowerSwitchState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestPowerSwitchState::transitionToHousing() const { return transitionToHousingState; }

void DeviceTestPowerSwitchState::onFail(void* ctx) {
    DeviceTestPowerSwitchState* state = static_cast<DeviceTestPowerSwitchState*>(ctx);
    state->session->result.components.powerSwitch = false;
    state->transitionToHousingState = true;
}

void DeviceTestPowerSwitchState::onPass(void* ctx) {
    DeviceTestPowerSwitchState* state = static_cast<DeviceTestPowerSwitchState*>(ctx);
    state->session->result.components.powerSwitch = true;
    state->transitionToHousingState = true;
}


DeviceTestHousingState::DeviceTestHousingState(DeviceTestSession* sessionIn)
    : TypedState<PDN>(DeviceTestStateId::HOUSING)
    , session(sessionIn) {}

DeviceTestHousingState::~DeviceTestHousingState() { session = nullptr; }

void DeviceTestHousingState::onStateMounted(PDN* pdn) {
    transitionToReportState = false;
    prompt.mount(pdn, "Housing", "PASS", "FAIL", this, onOk, onDamaged);
}

void DeviceTestHousingState::onStateDismounted(PDN* pdn) { prompt.dismount(pdn); }

bool DeviceTestHousingState::transitionToReport() const { return transitionToReportState; }

void DeviceTestHousingState::onDamaged(void* ctx) {
    DeviceTestHousingState* state = static_cast<DeviceTestHousingState*>(ctx);
    state->session->result.components.housing = false;
    state->transitionToReportState = true;
}

void DeviceTestHousingState::onOk(void* ctx) {
    DeviceTestHousingState* state = static_cast<DeviceTestHousingState*>(ctx);
    state->session->result.components.housing = true;
    state->transitionToReportState = true;
}


#define TAG "DEVICE_TEST_REPORT"

DeviceTestReportState::DeviceTestReportState(DeviceTestSession* sessionIn,
                                             DeviceTestWirelessManager* deviceTestWirelessManagerIn,
                                             WirelessManager* wirelessManagerIn)
    : TypedState<PDN>(DeviceTestStateId::REPORT)
    , session(sessionIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn)
    , wirelessManager(wirelessManagerIn) {}

DeviceTestReportState::~DeviceTestReportState() {
    session = nullptr;
    deviceTestWirelessManager = nullptr;
    wirelessManager = nullptr;
}

void DeviceTestReportState::renderStatus(PDN* pdn, const char* line1, const char* line2) {
    pdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawCenteredText(line1, 24)
    ->drawCenteredText(line2, 40)
    ->render();
}

void DeviceTestReportState::onStateMounted(PDN* pdn) {
    transitionToSelectHardwareState = false;
    uploadFinished = false;
    uploadSucceeded = false;
    uploadTimeout.setTimer(UPLOAD_TIMEOUT_MS);

    if (session->peerMacValid) {
        deviceTestWirelessManager->sendEndSession(session->peerMac.data());
    }

    renderStatus(pdn, "Uploading", "Please wait");

    if (session->result.inspectionId.isZero()) {
        char* inspectionText = IdGenerator::getInstance().generateId();
        IdGenerator::uuidStringToBytes(inspectionText, session->result.inspectionId.bytes);
        LOG_I(TAG, "Generated inspection UUID for this upload");
    }
    if (session->result.inspectedAtUnixMs <= 0) {
        PlatformClock* clock = SimpleTimer::getPlatformClock();
        if (clock != nullptr) {
            session->result.inspectedAtUnixMs = clock->unixMilliseconds();
            LOG_I(TAG, "Set inspectedAtUnixMs=%lld from platform clock",
                  static_cast<long long>(session->result.inspectedAtUnixMs));
        }
    }

    logDeviceReportPreflight(session->result);
    logDeviceReportPayload(session->result);

    DeviceTestResult resultCopy = session->result;
    DeviceInventoryApi::uploadDeviceReport(
    wirelessManager, resultCopy,
    [this](const std::string& response) {
        LOG_I(TAG, "Upload succeeded (HTTP 2xx, response %zu bytes)", response.size());
        uploadFinished = true;
        uploadSucceeded = true;
    },
    [this](const WirelessErrorInfo& error) {
        LOG_E(TAG,
              "Upload failed: %s (code=%d) ï¿½ see HttpClient logs for server body on HTTP errors",
              error.message.c_str(), static_cast<int>(error.code));
        uploadFinished = true;
        uploadSucceeded = false;
    });

    pdn->getSecondaryButton()->setButtonPress(
    [](void* ctx) {
        DeviceTestReportState* state = static_cast<DeviceTestReportState*>(ctx);
        if (state->uploadFinished) {
            state->transitionToSelectHardwareState = true;
        }
    },
    this, ButtonInteraction::CLICK);
}

void DeviceTestReportState::onStateLoop(PDN* pdn) {
    if (!uploadFinished) {
        if (uploadTimeout.expired()) {
            LOG_E(TAG, "Upload timed out after %lu ms",
                  static_cast<unsigned long>(UPLOAD_TIMEOUT_MS));
            uploadFinished = true;
            uploadSucceeded = false;
        }
        return;
    }
    if (uploadSucceeded) {
        renderStatus(pdn, "Uploaded", "");
    } else {
        renderStatus(pdn, "Upload failed", "");
    }
}

bool DeviceTestReportState::transitionToSelectHardware() const {
    return transitionToSelectHardwareState;
}

void DeviceTestReportState::onStateDismounted(PDN* pdn) {
    pdn->getSecondaryButton()->removeButtonCallbacks();
    *session = DeviceTestSession{};
}
