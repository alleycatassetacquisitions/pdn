#pragma once

#include "state/state-machine.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/quickdraw-states.hpp"

// The four gameplay state machines the PDN swaps between. Each owns the states
// it registers and knows the others only as an app id plus the QuickdrawStateId
// of the state to enter, never as a State*.

/**
 * The between-match app: Awaken -> Idle, Idle <-> SupporterReady, Sleep -> Awaken.
 * Every other app hands back here, and Idle declares every launch out of it.
 * Sleep has no inbound edge from within the hub — it is entered only by hand-off,
 * from the match app's Win or Lose and from a finished tournament. Registration is its
 * own top-level app, not a state in this one.
 */
class HubApp : public StateMachine {
public:
    /** Non-owning: the context's managers belong to the GameSession above it. */
    explicit HubApp(const GameContext& context);

    /**
     * Allocates the hub states and wires every edge leaving one, cross-app
     * edges included, in the priority order checkTransitions walks.
     */
    void populateStateMap() override;

private:
    GameContext context;
};

/**
 * MatchCountdown -> MatchState -> MatchPushed / MatchReceivedResult -> MatchResult ->
 * Win|Lose. Entered at MatchCountdown from the hub's Idle for a 1v1,
 * and from the shootout's bracket reveal or spectator for a tournament match.
 * Leaves to the hub's Idle on an abandoned match, to the hub's Sleep from Win
 * or Lose, and to the shootout on abort or a bracket outcome.
 */
class MatchApp : public StateMachine {
public:
    /** Non-owning: the context's managers belong to the GameSession above it. */
    explicit MatchApp(const GameContext& context);

    /**
     * Allocates the match states and wires every edge leaving one, cross-app
     * edges included, in the priority order checkTransitions walks.
     */
    void populateStateMap() override;

    /**
     * Ends the match. Runs only when the device leaves the match app entirely, which
     * is what "the match is over" means — the states inside it hand the match to
     * each other and must not tear it down on the way.
     */
    void onStateDismounted(Device* device) override;

private:
    GameContext context;
};

/**
 * Proposal -> BracketReveal, which either hands off to a bracket match or drops
 * to Spectator; Spectator and Eliminated both end at FinalStandings. Eliminated
 * has no inbound edge here — the match app hands into it. Aborted is the landing
 * state ten sources are pulled into: the hub's Idle, the five interruptible
 * match states, and four of the shootout states below.
 */
class ShootoutApp : public StateMachine {
public:
    /** Non-owning: the context's managers belong to the GameSession above it. */
    explicit ShootoutApp(const GameContext& context);

    /** Allocates the shootout states and wires every edge leaving one. */
    void populateStateMap() override;

private:
    GameContext context;
};

/**
 * Symbol -> SymbolMatched -> Symbol, and both states hand back to the hub's
 * Idle. Symbol's hand-off is its first edge, ahead of the local one into
 * SymbolMatched.
 */
class SymbolApp : public StateMachine {
public:
    /** Non-owning: the context's managers belong to the GameSession above it. */
    explicit SymbolApp(const GameContext& context);

    /** Allocates the symbol-match states and wires every edge leaving one. */
    void populateStateMap() override;

private:
    GameContext context;
};
