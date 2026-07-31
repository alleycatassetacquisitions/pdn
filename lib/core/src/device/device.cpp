#include "device/device.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/logger.hpp"
#include <utility>

const char* TAG = "Device";

void Device::loadAppConfig(AppConfig config, StateId launchAppId) {
    this->appConfig = std::move(config);
    this->currentAppId = launchAppId;
    if(appConfig.find(currentAppId) == appConfig.end()) {
        LOG_E(TAG, "App %d not found", currentAppId.id);
        return;
    }
    
    appConfig[currentAppId]->onStateMounted(this);
}

void Device::setActiveApp(StateId appId, int entryStateIndex) {
    if(appConfig.find(appId) == appConfig.end()) {
        LOG_E(TAG, "App %d not found", appId.id);
        return;
    }

    appConfig[currentAppId]->onStateDismounted(this);
    this->currentAppId = appId;
    // Set before the mount: apps override onStateMounted for their own setup and
    // chain to StateMachine::onStateMounted, so the entry slot cannot ride in as
    // an argument.
    appConfig[appId]->setEntryStateIndex(entryStateIndex);
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
