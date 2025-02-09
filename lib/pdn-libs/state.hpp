#pragma once

#include <functional>
#include <set>
#include <utility>
#include <vector>
#include <string>
#include <esp_log.h> // For logging

#include "device.hpp"

class State;

//StateId is a simple wrapper in case we eventually need to add more data to identify a state.
struct StateId {
    explicit StateId(int stateid) {
        id = stateid;
    }

    int id;
};

/*
 * A State transition is a tuple that holds a condition as well as
 * the state which the condition, when valid, will be transitioned to.
 *
 *  condition: A function that returns a boolean signifying the state machine
 *  should transition to the new state.
 *
 *  nextState: A pointer to the next valid state.
 */
class StateTransition {
public:
    // Constructor
    StateTransition(std::function<bool()> condition, State *nextState)
        : condition(condition), nextState(nextState) {
    };

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

/*
 * A state is meant to encapsulate a specific set of functionality within the context
 * of an application, ie quickdraw. States are broken up into a set of lifecycle methods.
 *
 * The process looks like this:
 *
 * onStateMounted: Invoked exactly one time the first time this state becomes
 * the "current" state in a state machine.
 *  - this method should include "setup" code for a state like starting timers,
 *  attaching callbacks to hardware, etc.
 *  - when mounting a new state is also when we register valid Serial messages for
 *  sending and receiving.
 *
 * onStateLoop: Invoked 1 to N times.
 * - this method is where the bulk of the functionality of a state occurs in.
 * - it is also where the conditions for state transitions will change and update.
 *
 * onStateDismounted: Invoked exactly 1 time, after any state transition condition
 * has been met.
 * - This method should be used for tearing down any long running logic and to reset
 * the conditions for state transitions - ie invalidating timers and resetting hardware
 * peripherals on the PDN.
 */
class State {
public:
    virtual ~State() {
        validStringMessages.erase(validStringMessages.begin(), validStringMessages.end());
        responseStringMessages.erase(responseStringMessages.begin(), responseStringMessages.end());
        transitions.erase(transitions.begin(), transitions.end());
    };

    explicit State(int stateId): name(stateId) {
    }

    void addTransition(StateTransition *transition) {
        transitions.push_back(transition);
    };

    State *checkTransitions() {
        for (StateTransition *transition: transitions) {
            ESP_LOGI("STATE", "Checking transition condition for state %d.", name.id);
            if (transition->isConditionMet()) {
                ESP_LOGI("STATE", "Transitioning to state %d.", transition->getNextState()->getStateId());
                return transition->nextState;
            }
        }
        return nullptr;
    };

    int getStateId() const { return name.id; };

    virtual void onStateMounted(Device *PDN) {
        ESP_LOGI("STATE", "State %d mounted.", name.id);
    };

    virtual void onStateLoop(Device *PDN) {
        ESP_LOGI("STATE", "State %d loop executed.", name.id);
    };

    virtual void onStateDismounted(Device *PDN) {
        ESP_LOGI("STATE", "State %d dismounted.", name.id);
    };

    virtual bool isTerminalState() {
        return false;
    }

    /*
     * Creates a set of valid String messages for this state.
     * Any message that is received that is *not* in this set
     * will be discarded.
     */
    virtual void registerValidMessages(std::vector<const string *> msgs) {
        for (auto msg: msgs) {
            ESP_LOGI("STATE", "Registering valid message: %s", msg->c_str());
            validStringMessages.insert(*msg);
        }
    };

    /*
     * This method registers valid messages that can be *sent* during this state's lifecycle.
     */
    virtual void registerResponseMessage(std::vector<const string *> msgs) {
        for (int i = 0; i < msgs.size(); i++) {
            ESP_LOGI("STATE", "Registering response message: %s", msgs.at(i)->c_str());
            responseStringMessages.push_back(*msgs.at(i));
        }
    };

    //Checks if the currently received String message is a part of the set of valid messages.
    bool isMessageValidForState(string *msg) {
        bool isValid = validStringMessages.find(*msg) != validStringMessages.end();
        ESP_LOGI("STATE", "Message %s is %s for state %d.", msg->c_str(), isValid ? "valid" : "invalid", name.id);
        return isValid;
    }

    /*
     * This message will consume the incoming Serial stream until a valid
     * message is found, at which point, that message will be returned.
     */
    string *waitForValidMessage(Device *PDN) {
        while (PDN->commsAvailable()) {
            if (!isMessageValidForState(PDN->peekComms())) {
                ESP_LOGI("STATE", "Discarding invalid message: %s", PDN->peekComms()->c_str());
                PDN->readString();
            } else {
                ESP_LOGI("STATE", "Valid message found: %s", PDN->peekComms()->c_str());
                return new string(PDN->readString());
            }
        }
        return nullptr;
    }

protected:
    std::set<string> validStringMessages;
    std::vector<string> responseStringMessages;
    std::vector<StateTransition *> transitions;

private:
    StateId name;
};
