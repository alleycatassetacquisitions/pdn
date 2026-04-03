#include "device/device.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/logger.hpp"

const char* TAG = "Device";

void Device::setAppManager(StateMachine* manager) {
    this->appManager = manager;
    this->appManager->onStateMounted(this);
}

void Device::loop() {
    driverManager.execDrivers();
    if (appManager) {
        appManager->onStateLoop(this);
    }
}
