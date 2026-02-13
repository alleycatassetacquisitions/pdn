#pragma once

#include <memory>
#include <functional>
#include <vector>

class Device;

class State;
class StateMachine;
/*
 * Snapshots are implemented per state. The class is defined by a state and utilized
 * when a state is paused, and passed back to the state in the onStateResume method.
 */
class Snapshot {
    virtual ~Snapshot() = default;
};

class StateMachineSnapshot : public Snapshot {
public:
    virtual ~StateMachineSnapshot() = default;

    Snapshot* getCurrentStateSnapshot() {
        return currentStateSnapshot;
    }

    void setCurrentStateSnapshot(Snapshot* snapshot) {
        currentStateSnapshot = snapshot;
    }

private:
    Snapshot* currentStateSnapshot;
};

//StateId is a simple wrapper in case we eventually need to add more data to identify a state.
// ***DO NOT USE NEGATIVE VALUES FOR STATE IDS, THESE ARE RESERVED FOR SYSTEM USE***
struct StateId {

    StateId() : id(0) {}
    explicit StateId(int stateid) {
        id = stateid;
    }

    bool operator<(const StateId& other) const {
        return id < other.id;
    }

    bool operator==(const StateId& other) const {
        return id == other.id;
    }

    int id;
};


class Stateful {
public:
    virtual ~Stateful() = default;

    StateId getName() const { return name; }

    int getStateId() const { return name.id; }

    StateId name;

    virtual void onStateMounted(Device* PDN) = 0;
    virtual std::unique_ptr<Snapshot> onStatePaused(Device* PDN) = 0;
    virtual void onStateResumed(Device* PDN, Snapshot* snapshot) = 0;
    virtual void onStateLoop(Device* PDN) = 0;
    virtual void onStateDismounted(Device* PDN) = 0;
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
    StateTransition(std::function<bool()> condition, StateId nextStateId)
        : condition(std::move(condition)), nextStateId(nextStateId) {
    }

    // Method to check if the transition condition is met
    bool isConditionMet() const {
        return condition();
    };

    // Getter for the next state
    StateId getNextStateId() const {
        return nextStateId;
    };

    std::function<bool()> condition; // Function pointer that returns true based on the global state
    StateId nextStateId; // Pointer to the next state
};

class NavigationInterface {
public:

    const StateId NO_TRANSITION = StateId(-1);

    virtual ~NavigationInterface() = default;
    
    virtual void addTransition(StateTransition* transition) {
        transitions.push_back(transition);
    }

    virtual void clearTransitions() {
        for (auto transition: transitions) {
            delete transition;
        }
        transitions.clear();
    }

    virtual StateId checkTransitions() {
        for (StateTransition *transition: transitions) {
            if (transition->isConditionMet()) {
                return transition->getNextStateId();
            }
        }
        return NO_TRANSITION;
    }

protected:
    std::vector<StateTransition*> transitions;
};