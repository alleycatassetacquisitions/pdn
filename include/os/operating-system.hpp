//
// Created by Elli Furedy on 10/16/2024.
//

#ifndef OPERATING_SYSTEM_HPP
#define OPERATING_SYSTEM_HPP

#include "state-machine.hpp"

enum OsStateId {
    DEBUG = 0,
    QUICKDRAW = 1,
};

class OperatingSystem : public StateMachine {
public:
    OperatingSystem();

    ~OperatingSystem() override;

    void populateStateMap() override;
};
#endif //OPERATING_SYSTEM_HPP
