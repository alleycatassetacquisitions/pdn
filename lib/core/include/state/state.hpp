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

    /// An edge that leaves the state machine entirely: the device dismounts the
    /// running app and mounts `targetAppId`, entering it at the state named by
    /// `entryStateId`. An unset id means the app's boot state, which is what an
    /// app transition that names no entry point gets.
    StateTransition(std::function<bool()> condition, StateId targetAppId, StateId entryStateId)
        : condition(std::move(condition))
        , targetAppId(targetAppId)
        , entryStateId(entryStateId) {
    }

    // Method to check if the transition condition is met
    bool isConditionMet() const {
        return condition();
    };

    // Getter for the next state
    State *getNextState() const {
        return nextState;
    };

    /// The app this edge hands off to; id < 0 when the edge stays inside the
    /// machine, in which case getNextState() carries the target instead.
    StateId getTargetAppId() const {
        return targetAppId;
    };

    /// The state in the target app the hand-off lands on, or an unset id for its
    /// boot state. Meaningless on an intra-machine edge.
    StateId getEntryStateId() const {
        return entryStateId;
    };

    std::function<bool()> condition; // Function pointer that returns true based on the global state
    State* nextState = nullptr;      // Pointer to the next state, null on an app transition
    /// Negative when this edge stays inside the machine.
    StateId targetAppId = StateId(-1);
    /// Negative to enter the target app at whichever state it registered first.
    StateId entryStateId = StateId(-1);
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
 * State inherits StateLifecycle, which is the dispatch interface StateMachine uses.
 * The mount/loop/dismount bridge methods are private so state implementers only ever
 * see and override the onState* user API below.
 *
 * Device-specific states should inherit TypedState<DeviceT> (defined below) which
 * delivers a typed DeviceT* to every onState* method, eliminating manual casting.
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

    /// Declares an edge to a sibling state in the same machine.
    void addTransition(std::function<bool()> condition, State* nextState) {
        transitions.push_back(new StateTransition(std::move(condition), nextState));
    }

    /// Declares an edge out of this state's app, entering the target at the state
    /// named by `entryStateId`. Omit it to enter at the target's boot state.
    ///
    /// App and intra-machine edges share one priority list, so an app transition
    /// declared before a sibling outranks it: the alternative — checking every
    /// intra edge first — would silently demote every hand-off below the local
    /// edges of the state it leaves.
    void addAppTransition(std::function<bool()> condition, StateId targetAppId,
                          StateId entryStateId = StateId(-1)) {
        transitions.push_back(new StateTransition(std::move(condition), targetAppId, entryStateId));
    }

    /// The first transition whose condition holds, or null when none do.
    StateTransition* checkTransitions() {
        for (StateTransition* transition : transitions) {
            if (transition->isConditionMet()) {
                return transition;
            }
        }
        return nullptr;
    }

    /// The transition list in priority order — checkTransitions takes the first
    /// whose condition holds, so position is behaviour. Reading it does not
    /// evaluate the conditions, which is what lets a graph be inspected before
    /// its managers exist.
    const std::vector<StateTransition*>& getTransitions() const { return transitions; }

    int getStateId() const { return name.id; }

    virtual bool isTerminalState() { return false; }

    // --- Device*-typed user API ---
    // Override these in derived classes.
    virtual void onStateMounted(Device* device) {}
    virtual void onStateLoop(Device* device) {}
    virtual void onStateDismounted(Device* device) {}

protected:
    std::vector<StateTransition*> transitions;

private:
    // StateLifecycle bridge — private so state subclasses cannot call or override
    // these entry points. StateMachine dispatches through StateLifecycle* to reach them.
    void mount(Device* device) override    { onStateMounted(device); }
    void loop(Device* device) override     { onStateLoop(device); }
    void dismount(Device* device) override { onStateDismounted(device); }

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
 * An intermediate base (ConnectState) that must run code around every subclass's
 * mount and dismount uses afterMount/beforeDismount. Overriding onStateMounted
 * instead would be silently shadowed by the concrete subclass's own override
 * unless every subclass remembered to chain to it.
 *
 * Usage:
 *   class IdleState : public TypedState<PDN> {
 *       void onStateMounted(PDN* pdn) override { ... }
 *       void onStateLoop(PDN* pdn) override    { ... }
 *   };
 *
 * Device-agnostic states should inherit State directly and override
 * onStateMounted(Device*).
 */
template<typename DeviceT>
class TypedState : public State {
public:
    using State::State;

    // Typed user API — override these in concrete state subclasses.
    virtual void onStateMounted(DeviceT* device) {}
    virtual void onStateLoop(DeviceT* device) {}
    virtual void onStateDismounted(DeviceT* device) {}

protected:
    // Bracket the subclass's own hooks. Ordering is the contract: afterMount runs
    // once onStateMounted has initialized the subclass, beforeDismount while it is
    // still live. For intermediate bases only, not concrete states.
    virtual void afterMount(DeviceT* device) {}
    virtual void beforeDismount(DeviceT* device) {}

private:
    // Private final bridge — casts once and forwards to the typed user API above.
    // State implementers cannot override or call these.
    void mount(Device* device) final {
        DeviceT* typed = static_cast<DeviceT*>(device);
        onStateMounted(typed);
        afterMount(typed);
    }
    void loop(Device* device) final {
        onStateLoop(static_cast<DeviceT*>(device));
    }
    void dismount(Device* device) final {
        DeviceT* typed = static_cast<DeviceT*>(device);
        beforeDismount(typed);
        onStateDismounted(typed);
    }
};
