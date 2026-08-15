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
    StateMachine* launch = getActiveApp();
    if (launch == nullptr) {
        LOG_E(TAG, "App %d not found", currentAppId.id);
        return;
    }

    // Stated rather than inherited, so a launch cannot come up at whatever a swap
    // last asked for. Both Device mount paths write it for that reason.
    launch->setEntryState(StateId(-1));
    launch->onStateMounted(this);
}

void Device::setActiveApp(StateId appId, StateId entryStateId) {
    auto target = appConfig.find(appId);
    if (target == appConfig.end()) {
        LOG_E(TAG, "App %d not found", appId.id);
        return;
    }

    // Looked up, not indexed: currentAppId can name an app this config does not hold
    // after a loadAppConfig whose launch id was missing, and operator[] would insert
    // a null there and dereference it.
    StateMachine* outgoing = getActiveApp();
    if (outgoing != nullptr) {
        outgoing->onStateDismounted(this);
    }

    this->currentAppId = appId;
    // Set before the mount: apps override onStateMounted for their own setup and
    // chain to StateMachine::onStateMounted, so the entry state cannot ride in as
    // an argument.
    target->second->setEntryState(entryStateId);
    target->second->onStateMounted(this);
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
