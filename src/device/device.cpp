#include "device/device.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/logger.hpp"
#include <utility>

const char* TAG = "Device";

Device::~Device() {
    // Dismount launched apps before deletion to ensure active state cleanup
    // (timers, callbacks, resources) runs properly. Only dismount apps that
    // were actually launched to respect the mount/dismount lifecycle contract.
    for (auto& pair : appConfig) {
        if (pair.second != nullptr) {
            if (pair.second->hasLaunched()) {
                pair.second->onStateDismounted(this);
            }
            delete pair.second;
            pair.second = nullptr;
        }
    }
    appConfig.clear();
    driverManager.dismountDrivers();
}

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

    previousAppId = currentAppId;
    appConfig[currentAppId]->onStatePaused(this);
    this->currentAppId = appId;
    if(appConfig[appId]->isPaused()) {
        appConfig[appId]->onStateResumed(this, nullptr);
    } else {
        appConfig[appId]->onStateMounted(this);
    }
}

void Device::returnToPreviousApp() {
    setActiveApp(previousAppId);
}

StateMachine* Device::getApp(StateId appId) {
    auto it = appConfig.find(appId);
    return (it != appConfig.end()) ? it->second : nullptr;
}

void Device::loop() {
    driverManager.execDrivers();
    auto app = appConfig.find(currentAppId);
    if(app != appConfig.end()) {
        app->second->onStateLoop(this);
    }
}
