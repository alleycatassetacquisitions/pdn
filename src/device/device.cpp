#include "device/device.hpp"
#include "state/state-machine.hpp"

void Device::loop() {
    driverManager.execDrivers();
    if (appConfig.count(currentAppId) > 0) {
        appConfig[currentAppId]->onStateLoop(this);
    }
}

void Device::loadAppConfig(AppConfig config, StateId launchAppId) {
    appConfig = config;
    currentAppId = launchAppId;

    if (appConfig.count(currentAppId) > 0) {
        appConfig[currentAppId]->onStateMounted(this);
    }
}

void Device::setActiveApp(StateId appId) {
    // Pause current app
    if (appConfig.count(currentAppId) > 0) {
        appConfig[currentAppId]->onStatePaused(this);
    }

    // Switch to new app
    currentAppId = appId;

    // Mount or resume the new app
    if (appConfig.count(currentAppId) > 0) {
        StateMachine* newApp = appConfig[currentAppId];
        if (newApp->hasLaunched()) {
            newApp->onStateResumed(this, nullptr);
        } else {
            newApp->onStateMounted(this);
        }
    }
}
