#pragma once

#include <functional>
#include <utility>
#include <vector>
#include <memory>

#include "state-types.hpp"
#include "state-lifecycle.hpp"

class State;
class Device;

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
        : condition(std::move(condition)), nextState(nextState) {
    }

    // Method to check if the transition condition is met
    bool isConditionMet() const {
        return condition();
    };

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
 *
 * State holds the transition machinery and the state id; it declares no lifecycle
 * hooks of its own. Concrete states inherit TypedState<DeviceT> (defined below),
 * which delivers a typed DeviceT* to every onState* method and keeps the
 * StateLifecycle bridge private so implementers cannot reach it.
 *
 * Device-agnostic states use TypedState<Device>.
 */
class State : public StateLifecycle {
public:
    explicit State(int stateId) : name(stateId) {}

    ~State() override {
        for (auto* transition : transitions) {
            delete transition;
        }
        transitions.clear();
    }

    void addTransition(StateTransition* transition) {
        transitions.push_back(transition);
    }

    void addAppTransition(std::function<bool()> condition, StateId targetAppId) {
        appTransitions.push_back({std::move(condition), targetAppId});
    }

    StateId checkAppTransitions() const {
        for (const auto& t : appTransitions) {
            if (t.condition()) return t.targetAppId;
        }
        return StateId(-1);
    }

    State* checkTransitions() {
        for (StateTransition* transition : transitions) {
            if (transition->isConditionMet()) {
                return transition->getNextState();
            }
        }
        return nullptr;
    }

    int getStateId() const { return name.id; }

    virtual bool isTerminalState() { return false; }

protected:
    std::vector<StateTransition*> transitions;

private:
    struct AppTransitionEntry {
        std::function<bool()> condition;
        StateId targetAppId;
    };
    std::vector<AppTransitionEntry> appTransitions;

    StateId name;
};

/*
 * TypedState<DeviceT> is the preferred base for device-specific states.
 *
 * It overrides the StateLifecycle bridge (mount/loop/dismount) as private final,
 * performs a single static_cast<DeviceT*> per lifecycle call, and forwards to
 * strongly-typed onState* virtual methods that concrete states override.
 *
 * The bridge methods are private so state implementers only ever interact with
 * the typed onStateMounted(DeviceT*) / onStateLoop(DeviceT*) / onStateDismounted(DeviceT*)
 * API — no manual casting needed and no accidental bridge override is possible.
 *
 * Usage:
 *   class IdleState : public TypedState<PDN> {
 *       void onStateMounted(PDN* pdn) override { ... }
 *       void onStateLoop(PDN* pdn) override    { ... }
 *   };
 *
 * Device-agnostic states (e.g. handshake states) use TypedState<Device>, whose
 * cast is an identity conversion.
 */
template<typename DeviceT>
class TypedState : public State {
public:
    using State::State;

    // Typed user API — override these in concrete state subclasses.
    virtual void onStateMounted(DeviceT* device) {}
    virtual void onStateLoop(DeviceT* device) {}
    virtual void onStateDismounted(DeviceT* device) {}

private:
    // Private final bridge — casts once and forwards to the typed user API above.
    // State implementers cannot override or call these.
    void mount(Device* device) final {
        onStateMounted(static_cast<DeviceT*>(device));
    }
    void loop(Device* device) final {
        onStateLoop(static_cast<DeviceT*>(device));
    }
    void dismount(Device* device) final {
        onStateDismounted(static_cast<DeviceT*>(device));
    }
};
