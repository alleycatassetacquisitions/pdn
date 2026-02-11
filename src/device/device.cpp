#include "device/device.hpp"
#include "state/state-machine.hpp"

void Device::loadAppConfig(AppConfig config, StateId launchAppId) {
    this->appConfig = config;
    this->currentAppId = launchAppId;
    this->appConfig[currentAppId]->onStateMounted(this);
}

void Device::setActiveApp(StateId appId) {
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
    if(!appConfig.empty()) {
        appConfig[currentAppId]->onStateLoop(this);
    }
}
