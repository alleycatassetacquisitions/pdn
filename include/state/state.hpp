#pragma once

#include <functional>
#include <set>
#include <utility>
#include <vector>
#include <string>
#include <memory>

#include "state-types.hpp"

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
class State : public Stateful, public NavigationInterface {
public:

    friend class StateMachine;
    virtual ~State() {
        for (auto* t : transitions) {
            delete t;
        }
        transitions.clear();
    }

    explicit State(int stateId) {
        name = StateId(stateId);
    }

    virtual void onMount(Device *PDN) {}
    virtual std::unique_ptr<Snapshot> onPause(Device *PDN) { return nullptr; }
    virtual void onResume(Device *PDN, Snapshot* snapshot) {}
    virtual void onLoop(Device *PDN) {}
    virtual void onDismount(Device *PDN) {}


protected:

    void onStateMounted(Device *PDN) override final {
        onMount(PDN);
    }

    std::unique_ptr<Snapshot> onStatePaused(Device *PDN) override final {
        return onPause(PDN);
    }

    void onStateResumed(Device *PDN, Snapshot* snapshot) override final {
        onResume(PDN, snapshot);
    }

    void onStateLoop(Device *PDN) override final {
        onLoop(PDN);
    }

    void onStateDismounted(Device *PDN) override final {
        onDismount(PDN);
    }
};
