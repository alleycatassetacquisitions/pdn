#pragma once

#include <map>
#include "state/state-machine.hpp"

constexpr int APP_MANAGER_APP_ID = 0;

using AppConfig = std::map<StateId, StateMachine*>;

class AppManager: public StateMachine {
public:
    AppManager(Device* PDN, AppConfig appConfig);
    ~AppManager();

    void populateStateMap() override;

private:
    Device* PDN;
    AppConfig appConfig;
};