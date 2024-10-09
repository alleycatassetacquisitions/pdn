//
// Created by Elli Furedy on 10/6/2024.
//
#pragma once
#include <gtest/gtest.h>

#include "device-mock.hpp"
#include "state-machine.hpp"

enum TestStateId {
    INITIAL_STATE = 0,
    SECOND_STATE = 1,
    THIRD_STATE = 2,
    TERMINAL_STATE = 99
};

const int INITIAL_TRANSITION_THRESHOLD = 1;
const int THIRD_TRANSITION_THRESHOLD = 3;
const int SECOND_TRANSITION_THRESHOLD = 2;
const int LOOPS_TO_COMPLETE_MACHINE = INITIAL_TRANSITION_THRESHOLD + SECOND_TRANSITION_THRESHOLD + THIRD_TRANSITION_THRESHOLD;

class InitialTestState : public State {
public:

    InitialTestState() : State(INITIAL_STATE) {
    }

    ~InitialTestState() {
    }

    void onStateMounted(Device *PDN) override {
        stateMountedInvoked = true;
    }

    void onStateLoop(Device *PDN) override {
        stateLoopInvoked++;
        if (stateLoopInvoked == INITIAL_TRANSITION_THRESHOLD) {
            transitionToSecond = true;
        }
    }

    void onStateDismounted(Device *PDN) override {
        stateDismountedInvoked = true;
        transitionToSecond = false;
    }

    bool transitionToSecondState() {
        return transitionToSecond;
    }

    std::vector<StateTransition *> getTransitions() {
        return transitions;
    }

    bool transitionToSecond = false;
    bool stateMountedInvoked = false;
    int stateLoopInvoked = 0;
    bool stateDismountedInvoked = false;
};

class SecondTestState : public State {
public:

    SecondTestState() : State(SECOND_STATE) {
    }

    ~SecondTestState() {
    }

    void onStateMounted(Device *PDN) override {
        stateMountedInvoked = true;
    }

    void onStateLoop(Device *PDN) override {
        stateLoopInvoked++;
        if (stateLoopInvoked == SECOND_TRANSITION_THRESHOLD) {
            transitionToThird = true;
        }
    }

    void onStateDismounted(Device *PDN) override {
        stateDismountedInvoked = true;
        transitionToThird = false;
    }

    bool transitionToThirdState() {
        return transitionToThird;
    }

    std::vector<StateTransition *> getTransitions() {
        return transitions;
    }


    bool stateMountedInvoked = false;
    int stateLoopInvoked = 0;
    bool stateDismountedInvoked = false;
    bool transitionToThird = false;
};

class ThirdTestState : public State {
public:

    ThirdTestState() : State(THIRD_STATE) {
    }

    ~ThirdTestState() {
    }

    void onStateMounted(Device *PDN) override {
        stateMountedInvoked = true;
    }

    void onStateLoop(Device *PDN) override {
        stateLoopInvoked++;
        if (stateLoopInvoked == THIRD_TRANSITION_THRESHOLD) {
            transitionToFourth = true;
        }
    }

    void onStateDismounted(Device *PDN) override {
        stateDismountedInvoked = true;
        transitionToFourth = false;
    }

    bool transitionToFourthState() {
        return transitionToFourth;
    }

    std::vector<StateTransition *> getTransitions() {
        return transitions;
    }


    bool stateMountedInvoked = false;
    int stateLoopInvoked = 0;
    bool stateDismountedInvoked = false;
    bool transitionToFourth = false;
};

class TerminalTestState : public State {
public:
    TerminalTestState() : State(TERMINAL_STATE) {
    }

    ~TerminalTestState() {
    }

    void onStateMounted(Device *PDN) override {
        stateMountedInvoked = true;
    }

    void onStateLoop(Device *PDN) override {
        stateLoopInvoked++;
        if(PDN->getCurrentVibrationIntensity() == VIBRATION_MAX) {
            transitionToSecond = true;
        }
        if(PDN->getCurrentVibrationIntensity() == VIBRATION_MAX) {
            transitionToFirst = true;
        }
    }

    void onStateDismounted(Device *PDN) override {
        stateDismountedInvoked = true;
        transitionToSecond = false;
    }

    bool transitionToSecondState() {
        return transitionToSecond;
    }

    bool transitionToFirstState() {
        return transitionToFirst;
    }

    std::vector<StateTransition *> getTransitions() {
        return transitions;
    }

    bool stateMountedInvoked = false;
    int stateLoopInvoked = 0;
    bool stateDismountedInvoked = false;
    bool transitionToSecond = false;
    bool transitionToFirst = false;
};


class TestStateMachine : public StateMachine {
public:

    TestStateMachine(MockDevice *PDN) : StateMachine(PDN) {
    }

    ~TestStateMachine() {
    }

    State *getStateFromStateMap(int position) {
        if (position >= stateMap.size()) {
            return nullptr;
        }

        return stateMap[position];
    }

    std::vector<State *> getStateMap() {
        return stateMap;
    };

    void populateStateMap() override {
        InitialTestState *initialState = new InitialTestState();
        SecondTestState *secondState = new SecondTestState();
        ThirdTestState *thirdState = new ThirdTestState();
        TerminalTestState *terminalState = new TerminalTestState();

        initialState->addTransition(
            new StateTransition(
                std::bind(
                    &InitialTestState::transitionToSecondState,
                    initialState),
                secondState));

        secondState->addTransition(
            new StateTransition(
                std::bind(
                    &SecondTestState::transitionToThirdState,
                    secondState),
                thirdState));

        thirdState->addTransition(
            new StateTransition(
                std::bind(
                    &ThirdTestState::transitionToFourthState,
                    thirdState),
                terminalState));

        terminalState->addTransition(
            new StateTransition(
                std::bind(
                    &TerminalTestState::transitionToSecondState,
                    terminalState),
                secondState));

        terminalState->addTransition(
            new StateTransition(
                std::bind(&TerminalTestState::transitionToFirstState,
                    terminalState),
                    initialState));

        stateMap.push_back(initialState);
        stateMap.push_back(secondState);
        stateMap.push_back(thirdState);
        stateMap.push_back(terminalState);
    }

    bool getTransitionReadyFlag() {
        return stateChangeReady;
    }

    State* getNewState() {
        return newState;
    }
};

class StateMachineTestSuite : public testing::Test {

protected:
    StateMachineTestSuite() {}

    void SetUp() override {
        stateMachine = new TestStateMachine(&stateMachineDevice);
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

    MockDevice stateMachineDevice;
    TestStateMachine* stateMachine;
};
