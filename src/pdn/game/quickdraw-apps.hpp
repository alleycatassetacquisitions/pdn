#pragma once

#include "state/state-machine.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/quickdraw-states.hpp"

// The four gameplay state machines the PDN swaps between. Each owns the states
// it registers and knows the others only as an app id plus an entry slot, never
// as a State*.
//
// The entry constants below are the addressing the cross-app edges use: an app
// transition names an index into the target's state map, so reordering a
// populateStateMap silently retargets every edge aiming into it. The state-graph
// test pins each order against the pre-split graph.

/// Awaken -> Idle, Idle <-> SupporterReady, Sleep -> Awaken. Sleep has no
/// inbound edge from within the hub: it is entered only by hand-off, from the
/// duel app's upload and from a finished tournament.
/// The between-match app — waking, waiting on a cable, waiting out someone
/// else's duel, asleep. Every other app hands back here, and Idle is where all
/// three launches out of it are declared. Registration is its own top-level
/// app, not a state in this one.
class HubApp : public StateMachine {
public:
    static constexpr int AWAKEN_SEQUENCE_INDEX = 0;
    static constexpr int IDLE_INDEX = 1;
    static constexpr int SUPPORTER_READY_INDEX = 2;
    static constexpr int SLEEP_INDEX = 3;

    /// Non-owning: the context's managers belong to the GameSession above it.
    explicit HubApp(const GameContext& context);

    /// Allocates the hub states and wires every edge leaving one, cross-app
    /// edges included, in the priority order checkTransitions walks.
    void populateStateMap() override;

private:
    GameContext context;
};

/// DuelCountdown -> Duel -> DuelPushed / DuelReceivedResult -> DuelResult ->
/// Win|Lose -> Upload. Entered at DuelCountdown from the hub's Idle for a 1v1,
/// and from the shootout's bracket reveal or spectator for a tournament match.
/// Leaves to the hub's Idle on an abandoned duel, to the hub's Sleep once the
/// upload finishes, and to the shootout on abort or a bracket outcome.
class DuelApp : public StateMachine {
public:
    static constexpr int DUEL_COUNTDOWN_INDEX = 0;

    /// Non-owning: the context's managers belong to the GameSession above it.
    explicit DuelApp(const GameContext& context);

    /// Allocates the duel states and wires every edge leaving one, cross-app
    /// edges included, in the priority order checkTransitions walks.
    void populateStateMap() override;

private:
    GameContext context;
};

/// Proposal -> BracketReveal, which either hands off to a bracket duel or drops
/// to Spectator; Spectator and Eliminated both end at FinalStandings. Eliminated
/// has no inbound edge here — the duel app hands into it. Aborted is the landing
/// state the five interruptible duel states and the hub's Idle are pulled into.
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

/// Symbol -> SymbolMatched -> Symbol | back to the hub's Idle.
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
