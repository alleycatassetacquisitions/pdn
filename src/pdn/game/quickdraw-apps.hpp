#pragma once

#include "state/state-machine.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/quickdraw-states.hpp"

// The three gameplay state machines the PDN swaps between. Each owns the states
// it registers and knows the others only as an app id plus an entry slot, never
// as a State*.
//
// The entry constants below are the addressing the cross-app edges use: an app
// transition names an index into the target's state map, so reordering a
// populateStateMap silently retargets every edge aiming into it. The state-graph
// test pins each order against the pre-split graph.

/// Registration -> Awaken -> Idle -> DuelCountdown -> Duel ->
/// DuelPushed / DuelReceivedResult -> DuelResult -> Win|Lose -> Upload -> Sleep.
/// The app the device spends most of its life in; the other two hand back here.
class DuelApp : public StateMachine {
public:
    static constexpr int AWAKEN_SEQUENCE_INDEX = 0;
    static constexpr int IDLE_INDEX = 1;
    static constexpr int DUEL_COUNTDOWN_INDEX = 3;
    static constexpr int SLEEP_INDEX = 11;

    /// Non-owning: the context's managers belong to the GameSession above it.
    explicit DuelApp(const GameContext& context);

    /// Allocates the duel states and wires every edge leaving one, cross-app
    /// edges included, in the priority order checkTransitions walks.
    void populateStateMap() override;

private:
    GameContext context;
};

/// Proposal -> BracketReveal -> Spectator | Eliminated -> FinalStandings, plus
/// the Aborted landing state every duel state can be pulled into.
class ShootoutApp : public StateMachine {
public:
    static constexpr int PROPOSAL_INDEX = 0;
    static constexpr int SPECTATOR_INDEX = 2;
    static constexpr int ELIMINATED_INDEX = 3;
    static constexpr int ABORTED_INDEX = 5;

    /// Non-owning: the context's managers belong to the GameSession above it.
    explicit ShootoutApp(const GameContext& context);

    /// Allocates the shootout states and wires every edge leaving one.
    void populateStateMap() override;

private:
    GameContext context;
};

/// Symbol -> SymbolMatched -> Symbol | back to the duel app's Idle.
class SymbolApp : public StateMachine {
public:
    static constexpr int SYMBOL_INDEX = 0;

    /// Non-owning: the context's managers belong to the GameSession above it.
    explicit SymbolApp(const GameContext& context);

    /// Allocates the symbol-match states and wires every edge leaving one.
    void populateStateMap() override;

private:
    GameContext context;
};
