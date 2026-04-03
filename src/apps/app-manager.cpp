#include "apps/app-manager.hpp"

AppManager::AppManager(Device* PDN, AppConfig appConfig) : StateMachine(APP_MANAGER_APP_ID) {
    this->PDN = PDN;
    this->appConfig = appConfig;
}

AppManager::~AppManager() {
    this->PDN = nullptr;
    this->appConfig.clear();
}

void AppManager::populateStateMap() {
}