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

void Device::setActiveApp(StateId appId) {
    if(appConfig.find(appId) == appConfig.end()) {
        LOG_E(TAG, "App %d not found", appId.id);
        return;
    }
    
    appConfig[currentAppId]->onStatePaused(this);
    this->currentAppId = appId;
    if(appConfig[appId]->isPaused()) {
        appConfig[appId]->onStateResumed(this, nullptr);
    } else {
        appConfig[appId]->onStateMounted(this);
    }
}

void Device::loop() {
    driverManager.execDrivers();
    auto app = appConfig.find(currentAppId);
    if(app != appConfig.end()) {
        app->second->onStateLoop(this);
    }
}
