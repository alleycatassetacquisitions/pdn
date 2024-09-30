#pragma once

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include "../device/device.hpp"

class State;

struct StateId {
    explicit StateId(int stateid) {
        id = stateid;
    }

    int id;
};

class StateTransition {
public:
    // Constructor
    StateTransition(std::function<bool()> condition, State *nextState)
        : condition(std::move(condition)), nextState(nextState) {};

    // Method to check if the transition condition is met
    bool isConditionMet() const {
        return condition();
    }

    // Getter for the next state
    State *getNextState() const {
        return nextState;
    };

    std::function<bool()> condition; // Function pointer that returns true based on the global state
    State *nextState; // Pointer to the next state
};

class State {
public:
    virtual ~State() {
        validStringMessages.erase(validStringMessages.begin(), validStringMessages.end());
        responseStringMessages.erase(responseStringMessages.begin(), responseStringMessages.end());
        transitions.erase(transitions.begin(), transitions.end());
    };

    explicit State(int stateId): name(stateId) {
    }

    virtual void addTransition(StateTransition *transition) {
        transitions.push_back(transition);
    };

    State *checkTransitions() {
        for (StateTransition* transition : transitions) {
            if (transition->isConditionMet()) {
                return transition->nextState;
            }
        }
        return nullptr;
    };

    int getName() const { return name; };

    virtual void onStateMounted(Device *PDN) {};

    virtual void onStateLoop(Device *PDN) {};

    virtual void onStateDismounted(Device *PDN) {};

    virtual void registerValidMessages(std::vector<const String*> msgs) {
        for (auto msg : msgs) {
            validStringMessages.insert(*msg);
        }
    };

    virtual void registerResponseMessage(std::vector<const String*> msgs) {
        for (int i = 0; i < msgs.size(); i++) {
            responseStringMessages.push_back(*msgs.at(i));
        }
    };

    bool isMessageValid(String* msg) {
        return validStringMessages.find(*msg) != validStringMessages.end();
    }

    String* waitForValidMessage(Device *PDN) {
        while(PDN->commsAvailable()) {
            if (!isMessageValid(PDN->peekComms())) {
                PDN->readString();
            } else {
                return new String(PDN->readString());
            }
        }
        return nullptr;
    }

protected:
    std::set<String> validStringMessages;
    std::vector<String> responseStringMessages;

private:
    int name;
    std::vector<StateTransition *> transitions;
};