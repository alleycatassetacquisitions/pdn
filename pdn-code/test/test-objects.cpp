//
// Created by Elli Furedy on 10/4/2024.
//
#include "state-machine/state-machine.hpp"

enum TestStateId {
    INITIAL_STATE = 1,
    SECOND_STATE = 2,
    THIRD_STATE = 3,
    TERMINAL_STATE = 99
};

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
        if (stateLoopInvoked == 1) {
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
        if (stateLoopInvoked == 2) {
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
        if (stateLoopInvoked == 3) {
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
    }

    void onStateDismounted(Device *PDN) override {
        stateDismountedInvoked = true;
        transitionToSecond = false;
    }

    bool transitionToSecondState() {
        return transitionToSecond;
    }


    bool stateMountedInvoked = false;
    int stateLoopInvoked = 0;
    bool stateDismountedInvoked = false;
    bool transitionToSecond = false;
};


class TestStateMachine : public StateMachine {
public:
    TestStateMachine(Device *PDN) : StateMachine(PDN) {
    }

    ~TestStateMachine() {
    }

    State *getStateFromStateMap(int position) {
        if (position >= stateMap.size()) {
            return nullptr;
        }

        return stateMap[position];
    }

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

        stateMap.push_back(initialState);
        stateMap.push_back(secondState);
        stateMap.push_back(thirdState);
        stateMap.push_back(terminalState);
    }
};
