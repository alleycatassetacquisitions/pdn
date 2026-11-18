#pragma once

#include "state/state-machine.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/quickdraw-states.hpp"

// The four gameplay state machines the PDN swaps between. Each owns the states
// it registers and knows the others only as an app id plus the QuickdrawStateId
// of the state to enter, never as a State*.

/** The between-match app; every other app hands back here. */
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

/** One duel from countdown to its result, for a 1v1 or a tournament match. */
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

/** The tournament around the bracket matches, plus Aborted, where an abort from any app lands. */
class ShootoutApp : public StateMachine {
public:
    /** Non-owning: the context's managers belong to the GameSession above it. */
    explicit ShootoutApp(const GameContext& context);

    /** Allocates the shootout states and wires every edge leaving one. */
    void populateStateMap() override;

private:
    GameContext context;
};

/** The symbol-matching app; both of its states hand back to the hub's Idle. */
class SymbolApp : public StateMachine {
public:
    /** Non-owning: the context's managers belong to the GameSession above it. */
    explicit SymbolApp(const GameContext& context);

    /** Allocates the symbol-match states and wires every edge leaving one. */
    void populateStateMap() override;

private:
    GameContext context;
};
