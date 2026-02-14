#pragma once

#include "state/state.hpp"

/*
 * Stub state for KonamiMetaGame.
 * This is a placeholder until other agents implement the actual states.
 */

class KonamiStubState : public State {
public:
    KonamiStubState() : State(9000) {}

    void onStateMounted(Device* PDN) override {
        // Stub - do nothing
    }

    void onStateLoop(Device* PDN) override {
        // Stub - do nothing
    }

    void onStateDismounted(Device* PDN) override {
        // Stub - do nothing
    }
};
