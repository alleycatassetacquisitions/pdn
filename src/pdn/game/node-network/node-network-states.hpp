#pragma once

#include "device/pdn.hpp"
#include "state/state.hpp"

enum NodeNetworkStateId {
    NODE_IDLE = 40,
};

/** First gameplay state after PlayerFetch; waits for a peer on the jacks. */
class NodeIdle : public TypedState<PDN> {
public:
    NodeIdle();
    ~NodeIdle() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
};
