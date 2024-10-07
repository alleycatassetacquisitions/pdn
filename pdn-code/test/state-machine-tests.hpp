//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once
#include <gtest/gtest.h>

#include "test-objects.cpp"

class StateMachineTestSuite : public testing::Test {

protected:
    StateMachineTestSuite() {}

    void SetUp() override {
        stateMachine = new TestStateMachine(&device);
    }

    void advanceStateMachineToState(int stateId) {
        int loopCount = 0;
        if(stateId == INITIAL_STATE) {
            return;
        }

        if(stateId == SECOND_STATE) {
            if(stateMachine->getCurrentState()->getStateId() == INITIAL_STATE) {
                loopCount = INITIAL_TRANSITION_THRESHOLD;
            } else {
                return;
            }
        } else if(stateId == THIRD_STATE) {
            if(stateMachine->getCurrentState()->getStateId() == INITIAL_STATE) {
                loopCount += INITIAL_TRANSITION_THRESHOLD;
                loopCount += SECOND_TRANSITION_THRESHOLD;
            } else if(stateMachine->getCurrentState()->getStateId() == SECOND_STATE) {
                loopCount = SECOND_TRANSITION_THRESHOLD;
            }
        } else if(stateId == TERMINAL_STATE) {
            if(stateMachine->getCurrentState()->getStateId() == INITIAL_STATE) {
                loopCount += LOOPS_TO_COMPLETE_MACHINE;
            } else if(stateMachine->getCurrentState()->getStateId() == SECOND_STATE) {
                loopCount += SECOND_TRANSITION_THRESHOLD;
                loopCount += THIRD_TRANSITION_THRESHOLD;
            } else if(stateMachine->getCurrentState()->getStateId() == THIRD_STATE) {
                loopCount = THIRD_TRANSITION_THRESHOLD;
            } else {
                return;
            }
        }

        while(loopCount--) {
            stateMachine->loop();
        }
    }

    MockDevice device;
    TestStateMachine* stateMachine;
};
