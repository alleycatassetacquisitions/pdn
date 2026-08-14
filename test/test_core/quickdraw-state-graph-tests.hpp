#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "apps/pdn-app-ids.hpp"
#include "apps/player-registration/player-registration.hpp"
#include "apps/player-registration/player-registration-states.hpp"
#include "game/quickdraw-apps.hpp"
#include "game/quickdraw-states.hpp"
#include "state/state.hpp"
#include "state/state-machine.hpp"

// The literals below are the pre-split flat graph, recorded before Quickdraw was
// broken into swappable apps, and they are the equivalence check for that split.
// Position in a transition list is priority, so it is behaviour, not style.
//
// They must not be "corrected" to match a future change: if a change is meant to
// alter the graph, that intent belongs in the diff to these literals.
//
// A null GameContext is sufficient and deliberate. Every state constructor only
// stores its pointers, and reading a transition list does not evaluate its
// condition, so the shape can be inspected without standing up any manager.

namespace quickdraw_state_graph_expectations {

// Registration order of the pre-split graph with its slot-0 PlayerRegistrationApp
// dropped — that app is now mounted by the device rather than nested as a state.
// The hub, duel, shootout and symbol maps concatenate to this.
//
// One deliberate divergence from the pre-split order: Sleep sat between
// UploadMatches and the shootout states, and now sits in the hub with the other
// three between-match states. Every edge is unchanged; only the flat position
// moved.
inline const std::vector<int> GAMEPLAY_REGISTRATION_ORDER = {
    AWAKEN_SEQUENCE,
    IDLE,
    SUPPORTER_READY,
    SLEEP,
    DUEL_COUNTDOWN,
    DUEL,
    DUEL_PUSHED,
    DUEL_RECEIVED_RESULT,
    DUEL_RESULT,
    WIN,
    LOSE,
    UPLOAD_MATCHES,
    SHOOTOUT_PROPOSAL,
    SHOOTOUT_BRACKET_REVEAL,
    SHOOTOUT_SPECTATOR,
    SHOOTOUT_ELIMINATED,
    SHOOTOUT_FINAL_STANDINGS,
    SHOOTOUT_ABORTED,
    SYMBOL,
    SYMBOL_MATCHED,
};

// Per-state transition targets, in the order they were added. 47 edges — the
// pre-split graph's 48 less PlayerRegistration -> AwakenSequence, which moved off
// the app object onto the state that triggered it (see
// registrationHandsOffFromWelcomeMessage below).
inline const std::vector<std::pair<int, std::vector<int>>> PRE_SPLIT_GAMEPLAY_EDGES = {
    {AWAKEN_SEQUENCE, {IDLE}},
    {IDLE,
     {SHOOTOUT_PROPOSAL, DUEL_COUNTDOWN, SUPPORTER_READY, SHOOTOUT_ABORTED, SYMBOL}},
    {SUPPORTER_READY, {IDLE}},
    {DUEL_COUNTDOWN, {SHOOTOUT_ABORTED, DUEL, IDLE}},
    {DUEL,
     {SHOOTOUT_ABORTED, SHOOTOUT_SPECTATOR, SHOOTOUT_ELIMINATED, IDLE,
      DUEL_RECEIVED_RESULT, DUEL_PUSHED}},
    {DUEL_PUSHED, {SHOOTOUT_ABORTED, IDLE, DUEL_RESULT}},
    {DUEL_RECEIVED_RESULT, {SHOOTOUT_ABORTED, IDLE, DUEL_RESULT}},
    {DUEL_RESULT,
     {SHOOTOUT_ABORTED, WIN, LOSE, SHOOTOUT_SPECTATOR, SHOOTOUT_ELIMINATED}},
    {WIN, {UPLOAD_MATCHES}},
    {LOSE, {UPLOAD_MATCHES}},
    {UPLOAD_MATCHES, {SLEEP}},
    {SLEEP, {AWAKEN_SEQUENCE}},
    {SHOOTOUT_PROPOSAL, {SHOOTOUT_BRACKET_REVEAL, SHOOTOUT_ABORTED}},
    {SHOOTOUT_BRACKET_REVEAL, {DUEL_COUNTDOWN, SHOOTOUT_SPECTATOR, SHOOTOUT_ABORTED}},
    {SHOOTOUT_SPECTATOR,
     {DUEL_COUNTDOWN, SHOOTOUT_FINAL_STANDINGS, SHOOTOUT_ABORTED}},
    {SHOOTOUT_ELIMINATED, {SHOOTOUT_FINAL_STANDINGS, SHOOTOUT_ABORTED}},
    {SHOOTOUT_FINAL_STANDINGS, {SLEEP}},
    {SHOOTOUT_ABORTED, {IDLE}},
    {SYMBOL, {IDLE, SYMBOL_MATCHED}},
    {SYMBOL_MATCHED, {SYMBOL, IDLE}},
};

// The edges the split turned into hand-offs, as (source state, position in that
// state's list) -> (target app, entry state). Every other edge in the table above
// stays inside its app. Pinned separately because the equivalence check reads
// through a hand-off to the state it lands on and so cannot tell the two apart.
//
// All 24 were intra-app edges pre-split, when the whole game was one machine.
inline const std::vector<std::pair<std::pair<int, size_t>, std::pair<int, int>>> CROSS_APP_EDGES = {
    {{IDLE, 0}, {SHOOTOUT_APP_ID, SHOOTOUT_PROPOSAL}},
    {{IDLE, 1}, {DUEL_APP_ID, DUEL_COUNTDOWN}},
    {{IDLE, 3}, {SHOOTOUT_APP_ID, SHOOTOUT_ABORTED}},
    {{IDLE, 4}, {SYMBOL_APP_ID, SYMBOL}},
    {{DUEL_COUNTDOWN, 0}, {SHOOTOUT_APP_ID, SHOOTOUT_ABORTED}},
    {{DUEL_COUNTDOWN, 2}, {HUB_APP_ID, IDLE}},
    {{DUEL, 0}, {SHOOTOUT_APP_ID, SHOOTOUT_ABORTED}},
    {{DUEL, 1}, {SHOOTOUT_APP_ID, SHOOTOUT_SPECTATOR}},
    {{DUEL, 2}, {SHOOTOUT_APP_ID, SHOOTOUT_ELIMINATED}},
    {{DUEL, 3}, {HUB_APP_ID, IDLE}},
    {{DUEL_PUSHED, 0}, {SHOOTOUT_APP_ID, SHOOTOUT_ABORTED}},
    {{DUEL_PUSHED, 1}, {HUB_APP_ID, IDLE}},
    {{DUEL_RECEIVED_RESULT, 0}, {SHOOTOUT_APP_ID, SHOOTOUT_ABORTED}},
    {{DUEL_RECEIVED_RESULT, 1}, {HUB_APP_ID, IDLE}},
    {{DUEL_RESULT, 0}, {SHOOTOUT_APP_ID, SHOOTOUT_ABORTED}},
    {{DUEL_RESULT, 3}, {SHOOTOUT_APP_ID, SHOOTOUT_SPECTATOR}},
    {{DUEL_RESULT, 4}, {SHOOTOUT_APP_ID, SHOOTOUT_ELIMINATED}},
    {{UPLOAD_MATCHES, 0}, {HUB_APP_ID, SLEEP}},
    {{SHOOTOUT_BRACKET_REVEAL, 0}, {DUEL_APP_ID, DUEL_COUNTDOWN}},
    {{SHOOTOUT_SPECTATOR, 0}, {DUEL_APP_ID, DUEL_COUNTDOWN}},
    {{SHOOTOUT_FINAL_STANDINGS, 0}, {HUB_APP_ID, SLEEP}},
    {{SHOOTOUT_ABORTED, 0}, {HUB_APP_ID, IDLE}},
    {{SYMBOL, 0}, {HUB_APP_ID, IDLE}},
    {{SYMBOL_MATCHED, 1}, {HUB_APP_ID, IDLE}},
};

}  // namespace quickdraw_state_graph_expectations

// Holds the five apps, populated but never mounted. Each app owns the states it
// registered and frees them; this only owns the apps.
struct QuickdrawAppsForTest {
    PlayerRegistrationApp* playerRegistration = nullptr;
    HubApp* hub = nullptr;
    DuelApp* duel = nullptr;
    ShootoutApp* shootout = nullptr;
    SymbolApp* symbol = nullptr;

    /// Constructs and populates the five apps without mounting any of them.
    QuickdrawAppsForTest() {
        GameContext context;
        playerRegistration = new PlayerRegistrationApp(nullptr, nullptr, nullptr, nullptr);
        hub = new HubApp(context);
        duel = new DuelApp(context);
        shootout = new ShootoutApp(context);
        symbol = new SymbolApp(context);
        playerRegistration->populateStateMap();
        hub->populateStateMap();
        duel->populateStateMap();
        shootout->populateStateMap();
        symbol->populateStateMap();
    }

    /// Frees the apps, and with them every state they registered.
    ~QuickdrawAppsForTest() {
        delete playerRegistration;
        delete hub;
        delete duel;
        delete shootout;
        delete symbol;
    }

    /// The gameplay apps' states in registration order: hub, duel, shootout,
    /// symbol.
    std::vector<State*> gameplayStates() const {
        std::vector<State*> states;
        for (const StateMachine* app : {static_cast<const StateMachine*>(hub),
                                        static_cast<const StateMachine*>(duel),
                                        static_cast<const StateMachine*>(shootout),
                                        static_cast<const StateMachine*>(symbol)}) {
            for (State* state : app->getStateMap()) {
                states.push_back(state);
            }
        }
        return states;
    }

    /// The same states, keyed by state id.
    std::map<int, State*> statesById() const {
        std::map<int, State*> byId;
        for (State* state : gameplayStates()) {
            byId[state->getStateId()] = state;
        }
        return byId;
    }

    /// Where an edge actually lands: an intra-app edge names its target
    /// directly, a hand-off names an app plus the state id to enter it at.
    State* resolveTarget(const StateTransition* edge) const {
        if (edge->getNextState() != nullptr) return edge->getNextState();
        const StateMachine* target = nullptr;
        if (edge->getTargetAppId().id == HUB_APP_ID) target = hub;
        if (edge->getTargetAppId().id == DUEL_APP_ID) target = duel;
        if (edge->getTargetAppId().id == SHOOTOUT_APP_ID) target = shootout;
        if (edge->getTargetAppId().id == SYMBOL_APP_ID) target = symbol;
        if (target == nullptr) return nullptr;
        for (State* state : target->getStateMap()) {
            if (state->getStateId() == edge->getEntryStateId().id) return state;
        }
        return nullptr;
    }
};

// The concatenated app maps are the declared registration order, which is the
// pre-split order with Sleep moved into the hub. Each app's stateMap[0] is what a
// mount with no named entry state enters at, which is the one position a reorder
// can still change the behaviour of.
inline void quickdrawAppsRegisterStatesInDeclaredOrder() {
    QuickdrawAppsForTest apps;

    const std::vector<int>& expected =
        quickdraw_state_graph_expectations::GAMEPLAY_REGISTRATION_ORDER;
    std::vector<State*> actual = apps.gameplayStates();
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i]->getStateId(), expected[i])
            << "registration slot " << i << " changed";
    }

    EXPECT_EQ(apps.hub->getStateMap()[0]->getStateId(), AWAKEN_SEQUENCE);
    EXPECT_EQ(apps.duel->getStateMap()[0]->getStateId(), DUEL_COUNTDOWN);
    EXPECT_EQ(apps.shootout->getStateMap()[0]->getStateId(), SHOOTOUT_PROPOSAL);
    EXPECT_EQ(apps.symbol->getStateMap()[0]->getStateId(), SYMBOL);
}

// Every edge of the pre-split graph still leaves the same state, at the same
// position in its list, for the same target — reading a hand-off through to the
// state it enters. Transition position is priority: checkTransitions returns the
// first condition that holds, so demoting an edge below a sibling that can be
// true at the same moment silently disables it.
inline void quickdrawAppEdgesMatchPreSplitGraph() {
    QuickdrawAppsForTest apps;
    std::map<int, State*> byId = apps.statesById();

    size_t totalEdges = 0;
    for (const std::pair<int, std::vector<int>>& expected :
         quickdraw_state_graph_expectations::PRE_SPLIT_GAMEPLAY_EDGES) {
        State* source = byId.count(expected.first) ? byId[expected.first] : nullptr;
        ASSERT_NE(source, nullptr) << "state " << expected.first << " missing";

        const std::vector<StateTransition*>& actual = source->getTransitions();
        ASSERT_EQ(actual.size(), expected.second.size())
            << "state " << expected.first << " transition count changed";
        for (size_t i = 0; i < expected.second.size(); ++i) {
            State* target = apps.resolveTarget(actual[i]);
            ASSERT_NE(target, nullptr)
                << "state " << expected.first << " edge " << i << " resolves nowhere";
            EXPECT_EQ(target->getStateId(), expected.second[i])
                << "state " << expected.first << " edge " << i << " retargeted";
        }
        totalEdges += expected.second.size();
    }
    EXPECT_EQ(totalEdges, 47u);
}

// Which of those edges became hand-offs, and the app plus entry state each names.
// The equivalence check above already catches an edge that loses its entry state or
// names the wrong app: resolveTarget scans only the named app's map, the four maps
// hold disjoint id ranges, so either mistake resolves nowhere and fails there. What
// only this table pins is that an edge is a hand-off rather than an intra-app
// transition — an intra edge that quietly became one still lands on the same state.
inline void quickdrawCrossAppEdgesAreAppTransitions() {
    QuickdrawAppsForTest apps;
    std::map<int, State*> byId = apps.statesById();

    std::map<int, std::vector<size_t>> expectedAppEdgesBySource;
    for (const std::pair<std::pair<int, size_t>, std::pair<int, int>>& expected :
         quickdraw_state_graph_expectations::CROSS_APP_EDGES) {
        int sourceId = expected.first.first;
        size_t position = expected.first.second;
        expectedAppEdgesBySource[sourceId].push_back(position);

        State* source = byId.count(sourceId) ? byId[sourceId] : nullptr;
        ASSERT_NE(source, nullptr) << "state " << sourceId << " missing";
        ASSERT_LT(position, source->getTransitions().size());

        const StateTransition* edge = source->getTransitions()[position];
        EXPECT_EQ(edge->getNextState(), nullptr)
            << "state " << sourceId << " edge " << position << " is no longer a hand-off";
        EXPECT_EQ(edge->getTargetAppId().id, expected.second.first)
            << "state " << sourceId << " edge " << position << " changed target app";
        EXPECT_EQ(edge->getEntryStateId().id, expected.second.second)
            << "state " << sourceId << " edge " << position << " changed entry state";
    }

    // No edge outside the table above is a hand-off: an intra-app edge that
    // quietly became one would restart the target app instead of stepping.
    for (const std::pair<const int, State*>& entry : byId) {
        const std::vector<size_t>& appEdges = expectedAppEdgesBySource[entry.first];
        for (size_t i = 0; i < entry.second->getTransitions().size(); ++i) {
            bool expectedApp =
                std::find(appEdges.begin(), appEdges.end(), i) != appEdges.end();
            EXPECT_EQ(entry.second->getTransitions()[i]->getNextState() == nullptr, expectedApp)
                << "state " << entry.first << " edge " << i << " changed kind";
        }
    }
}

// The pre-split graph's 48th edge. It used to sit on the PlayerRegistrationApp
// object, guarded by "the current state is WelcomeMessage and it wants
// gameplay"; nothing checks a top-level app's own transition list, so it moved
// onto WelcomeMessage, which was its only trigger.
inline void registrationHandsOffFromWelcomeMessage() {
    QuickdrawAppsForTest apps;

    State* welcomeMessage = nullptr;
    for (State* state : apps.playerRegistration->getStateMap()) {
        if (state->getStateId() == PlayerRegistrationStateId::WELCOME_MESSAGE) {
            welcomeMessage = state;
        }
    }
    ASSERT_NE(welcomeMessage, nullptr);
    ASSERT_EQ(welcomeMessage->getTransitions().size(), 1u);

    const StateTransition* edge = welcomeMessage->getTransitions()[0];
    EXPECT_EQ(edge->getNextState(), nullptr);
    EXPECT_EQ(edge->getTargetAppId().id, HUB_APP_ID);
    // Registration names no entry state: it hands control to the hub and the hub
    // decides where that lands. Naming one would put a gameplay state id in the
    // registration app, which is the coupling the two apps do not otherwise have.
    EXPECT_LT(edge->getEntryStateId().id, 0);
    EXPECT_EQ(apps.hub->getStateMap()[0]->getStateId(), AWAKEN_SEQUENCE);
}
