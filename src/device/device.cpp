#include "device/device.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/logger.hpp"
#include <utility>

const char* TAG = "Device";

void Device::loadAppConfig(AppConfig config, AppId launchAppId) {
    this->appConfig = std::move(config);
    this->currentAppId = std::move(launchAppId);
    if(appConfig.find(currentAppId) == appConfig.end()) {
        LOG_E(TAG, "App %d not found", currentAppId);
        return;
    }
    
    appConfig[currentAppId]->onStateMounted(this);
}

void Device::setActiveApp(AppId appId) {
    if(appConfig.find(appId) == appConfig.end()) {
        LOG_E(TAG, "App %d not found", static_cast<int>(appId));
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
