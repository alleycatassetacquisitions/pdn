#include "device/device.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/logger.hpp"
#include <utility>

const char* TAG = "Device";

void Device::loadAppConfig(AppConfig config, StateId launchAppId) {
    // Resolved against the outgoing config, so it has to run before the move. A
    // reconfiguration otherwise leaves the previous app's state mounted with nothing
    // able to reach it again — two live mounts on one device.
    StateMachine* mounted = getActiveApp();
    if (mounted != nullptr) {
        mounted->onStateDismounted(this);
    }

    this->appConfig = std::move(config);
    this->currentAppId = launchAppId;
    if(appConfig.find(currentAppId) == appConfig.end()) {
        LOG_E(TAG, "App %d not found", currentAppId.id);
        return;
    }

    // Stated rather than inherited, so a launch cannot come up at whatever a swap
    // last asked for. Both Device mount paths write it for that reason.
    appConfig[currentAppId]->setEntryState(StateId(-1));
    appConfig[currentAppId]->onStateMounted(this);
}

void Device::setActiveApp(StateId appId, StateId entryStateId) {
    if(appConfig.find(appId) == appConfig.end()) {
        LOG_E(TAG, "App %d not found", appId.id);
        return;
    }

    appConfig[currentAppId]->onStateDismounted(this);
    this->currentAppId = appId;
    // Set before the mount: apps override onStateMounted for their own setup and
    // chain to StateMachine::onStateMounted, so the entry state cannot ride in as
    // an argument.
    appConfig[appId]->setEntryState(entryStateId);
    appConfig[appId]->onStateMounted(this);
}

StateMachine* Device::getActiveApp() {
    auto app = appConfig.find(currentAppId);
    return app != appConfig.end() ? app->second : nullptr;
}

void Device::setTickCallback(std::function<void()> tickCallback) {
    this->tickCallback = std::move(tickCallback);
}

void Device::loop() {
    driverManager.execDrivers();
    if (tickCallback) {
        tickCallback();
    }
    auto app = appConfig.find(currentAppId);
    if(app != appConfig.end()) {
        app->second->onStateLoop(this);
    }
}
