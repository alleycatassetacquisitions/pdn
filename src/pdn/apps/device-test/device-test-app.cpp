#include "apps/device-test/device-test-app.hpp"

DeviceTestApp::DeviceTestApp(RemoteDeviceCoordinator* remoteDeviceCoordinatorIn,
                             DeviceTestWirelessManager* deviceTestWirelessManagerIn,
                             WirelessManager* wirelessManagerIn, int stateId)
    : StateMachine(stateId)
    , remoteDeviceCoordinator(remoteDeviceCoordinatorIn)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn)
    , wirelessManager(wirelessManagerIn) {}

DeviceTestApp::~DeviceTestApp() {
    remoteDeviceCoordinator = nullptr;
    deviceTestWirelessManager = nullptr;
    wirelessManager = nullptr;
}

void DeviceTestApp::onStateMounted(Device* device) {
    if (deviceTestWirelessManager != nullptr) {
        deviceTestWirelessManager->registerEspNowHandler();
    }
    StateMachine::onStateMounted(device);
}

void DeviceTestApp::onStateDismounted(Device* device) {
    StateMachine::onStateDismounted(device);
    if (deviceTestWirelessManager != nullptr) {
        deviceTestWirelessManager->clearCallbacks();
        deviceTestWirelessManager->unregisterEspNowHandler();
    }
}

void DeviceTestApp::populateStateMap() {
    if (!stateMap.empty()) {
        return;
    }
    DeviceTestSelectHardwareState* selectHardware = new DeviceTestSelectHardwareState(&session);
    DeviceTestEnterLabelState* enterLabel = new DeviceTestEnterLabelState(&session);
    DeviceTestAwaitOutputJackState* awaitOutputJack = new DeviceTestAwaitOutputJackState(
    &session, remoteDeviceCoordinator, deviceTestWirelessManager);
    DeviceTestRemediateInputJackState* remediateInputJack = new DeviceTestRemediateInputJackState(
    &session, remoteDeviceCoordinator, deviceTestWirelessManager);
    DeviceTestAwaitSessionInfoState* awaitSessionInfo =
    new DeviceTestAwaitSessionInfoState(&session, deviceTestWirelessManager);
    DeviceTestPromptCheckState* screenCheck = new DeviceTestPromptCheckState(
    DeviceTestStateId::SCREEN_CHECK, &session, deviceTestWirelessManager, "Screen OK?",
    DeviceTestCommand::SHOW_SCREEN_PATTERN, &DeviceTestComponents::screen);
    DeviceTestPromptCheckState* ledCheck = new DeviceTestPromptCheckState(
    DeviceTestStateId::LED_CHECK, &session, deviceTestWirelessManager, "LEDs OK?",
    DeviceTestCommand::SHOW_ALL_LEDS, &DeviceTestComponents::leds);
    DeviceTestButtonCheckState* buttonPrimary = new DeviceTestButtonCheckState(
    DeviceTestStateId::BUTTON_PRIMARY, &session, deviceTestWirelessManager,
    ButtonIdentifier::PRIMARY_BUTTON, &DeviceTestComponents::primaryButton);
    DeviceTestButtonCheckState* buttonSecondary = new DeviceTestButtonCheckState(
    DeviceTestStateId::BUTTON_SECONDARY, &session, deviceTestWirelessManager,
    ButtonIdentifier::SECONDARY_BUTTON, &DeviceTestComponents::secondaryButton);
    DeviceTestPromptCheckState* hapticsCheck = new DeviceTestPromptCheckState(
    DeviceTestStateId::HAPTICS_CHECK, &session, deviceTestWirelessManager, "Haptics OK?",
    DeviceTestCommand::TRIGGER_HAPTICS, &DeviceTestComponents::haptics);
    DeviceTestAwaitInputJackState* awaitInputJack =
    new DeviceTestAwaitInputJackState(&session, remoteDeviceCoordinator);
    DeviceTestPowerSwitchState* powerSwitch =
    new DeviceTestPowerSwitchState(&session, remoteDeviceCoordinator, deviceTestWirelessManager);
    DeviceTestHousingState* housing = new DeviceTestHousingState(&session);
    DeviceTestReportState* report =
    new DeviceTestReportState(&session, deviceTestWirelessManager, wirelessManager);

    selectHardware->addTransition(new StateTransition(
    [selectHardware]() { return selectHardware->transitionToEnterLabel(); }, enterLabel));

    enterLabel->addTransition(new StateTransition(
    [enterLabel]() { return enterLabel->transitionToAwaitOutputJack(); }, awaitOutputJack));

    awaitOutputJack->addTransition(new StateTransition(
    [awaitOutputJack]() { return awaitOutputJack->transitionToAwaitSessionInfo(); },
    awaitSessionInfo));
    awaitOutputJack->addTransition(new StateTransition(
    [awaitOutputJack]() { return awaitOutputJack->transitionToRemediateInputJack(); },
    remediateInputJack));

    remediateInputJack->addTransition(new StateTransition(
    [remediateInputJack]() { return remediateInputJack->transitionToAwaitSessionInfo(); },
    awaitSessionInfo));
    remediateInputJack->addTransition(new StateTransition(
    [remediateInputJack]() { return remediateInputJack->transitionToPowerSwitch(); }, powerSwitch));

    awaitSessionInfo->addTransition(new StateTransition(
    [awaitSessionInfo]() { return awaitSessionInfo->transitionToScreenCheck(); }, screenCheck));
    awaitSessionInfo->addTransition(new StateTransition(
    [awaitSessionInfo]() { return awaitSessionInfo->transitionToPowerSwitch(); }, powerSwitch));

    screenCheck->addTransition(
    new StateTransition([screenCheck]() { return screenCheck->transitionToNext(); }, ledCheck));
    ledCheck->addTransition(
    new StateTransition([ledCheck]() { return ledCheck->transitionToNext(); }, buttonPrimary));
    buttonPrimary->addTransition(new StateTransition(
    [buttonPrimary]() { return buttonPrimary->transitionToNext(); }, buttonSecondary));
    buttonSecondary->addTransition(new StateTransition(
    [buttonSecondary]() { return buttonSecondary->transitionToNext(); }, hapticsCheck));

    DeviceTestSession* sessionPtr = &session;
    hapticsCheck->addTransition(new StateTransition(
    [hapticsCheck, sessionPtr]() {
        return hapticsCheck->transitionToNext() && !sessionPtr->inputJackTested;
    },
    awaitInputJack));
    hapticsCheck->addTransition(new StateTransition(
    [hapticsCheck, sessionPtr]() {
        return hapticsCheck->transitionToNext() && sessionPtr->inputJackTested;
    },
    powerSwitch));

    awaitInputJack->addTransition(new StateTransition(
    [awaitInputJack]() { return awaitInputJack->transitionToPowerSwitch(); }, powerSwitch));

    powerSwitch->addTransition(
    new StateTransition([powerSwitch]() { return powerSwitch->transitionToHousing(); }, housing));
    housing->addTransition(
    new StateTransition([housing]() { return housing->transitionToReport(); }, report));
    report->addTransition(new StateTransition(
    [report]() { return report->transitionToSelectHardware(); }, selectHardware));

    stateMap.push_back(selectHardware);
    stateMap.push_back(enterLabel);
    stateMap.push_back(awaitOutputJack);
    stateMap.push_back(remediateInputJack);
    stateMap.push_back(awaitSessionInfo);
    stateMap.push_back(screenCheck);
    stateMap.push_back(ledCheck);
    stateMap.push_back(buttonPrimary);
    stateMap.push_back(buttonSecondary);
    stateMap.push_back(hapticsCheck);
    stateMap.push_back(awaitInputJack);
    stateMap.push_back(powerSwitch);
    stateMap.push_back(housing);
    stateMap.push_back(report);
}
