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

// The graph these tests pin is otherwise unexecuted by test_core: nothing else
// there calls a gameplay app's populateStateMap. (test_cli does, by building a
// device.) Both orders below are behaviour, not
// style — a mount enters stateMap[0] unless an app transition names another
// slot, and State::checkTransitions returns the first transition whose condition
// holds, so a reordering here silently reroutes the device while every other
// test stays green.
//
// The literals are the pre-split flat graph, recorded before Quickdraw was
// broken into swappable apps, and are the equivalence check for that split: the
// concatenated app state maps must still read as the old registration order, and
// every edge must still land on the same state at the same priority — whether it
// is now an intra-app transition or a hand-off naming an app plus an entry slot.
// They must not be "corrected" to match a future change: if a change is meant to
// alter the graph, that intent belongs in the diff to these literals.
//
// A null GameContext is sufficient and deliberate. Every state constructor only
// stores its pointers, and reading a transition list does not evaluate its
// condition, so the shape can be inspected without standing up any manager.

namespace quickdraw_state_graph_expectations {

// Registration order of the pre-split graph with its slot-0 PlayerRegistrationApp
// dropped — that app is now mounted by the device rather than nested as a state.
// The duel, shootout and symbol maps concatenate to exactly this.
inline const std::vector<int> PRE_SPLIT_GAMEPLAY_ORDER = {
    AWAKEN_SEQUENCE,
    IDLE,
    SUPPORTER_READY,
    DUEL_COUNTDOWN,
    DUEL,
    DUEL_PUSHED,
    DUEL_RECEIVED_RESULT,
    DUEL_RESULT,
    WIN,
    LOSE,
    UPLOAD_MATCHES,
    SLEEP,
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
// state's list) -> (target app, entry slot). Every other edge in the table above
// stays inside its app. Pinned separately because the equivalence check reads
// through a hand-off to the state it lands on and so cannot tell the two apart.
inline const std::vector<std::pair<std::pair<int, size_t>, std::pair<int, int>>> CROSS_APP_EDGES = {
    {{IDLE, 0}, {SHOOTOUT_APP_ID, ShootoutApp::PROPOSAL_INDEX}},
    {{IDLE, 3}, {SHOOTOUT_APP_ID, ShootoutApp::ABORTED_INDEX}},
    {{IDLE, 4}, {SYMBOL_APP_ID, SymbolApp::SYMBOL_INDEX}},
    {{DUEL_COUNTDOWN, 0}, {SHOOTOUT_APP_ID, ShootoutApp::ABORTED_INDEX}},
    {{DUEL, 0}, {SHOOTOUT_APP_ID, ShootoutApp::ABORTED_INDEX}},
    {{DUEL, 1}, {SHOOTOUT_APP_ID, ShootoutApp::SPECTATOR_INDEX}},
    {{DUEL, 2}, {SHOOTOUT_APP_ID, ShootoutApp::ELIMINATED_INDEX}},
    {{DUEL_PUSHED, 0}, {SHOOTOUT_APP_ID, ShootoutApp::ABORTED_INDEX}},
    {{DUEL_RECEIVED_RESULT, 0}, {SHOOTOUT_APP_ID, ShootoutApp::ABORTED_INDEX}},
    {{DUEL_RESULT, 0}, {SHOOTOUT_APP_ID, ShootoutApp::ABORTED_INDEX}},
    {{DUEL_RESULT, 3}, {SHOOTOUT_APP_ID, ShootoutApp::SPECTATOR_INDEX}},
    {{DUEL_RESULT, 4}, {SHOOTOUT_APP_ID, ShootoutApp::ELIMINATED_INDEX}},
    {{SHOOTOUT_BRACKET_REVEAL, 0}, {DUEL_APP_ID, DuelApp::DUEL_COUNTDOWN_INDEX}},
    {{SHOOTOUT_SPECTATOR, 0}, {DUEL_APP_ID, DuelApp::DUEL_COUNTDOWN_INDEX}},
    {{SHOOTOUT_FINAL_STANDINGS, 0}, {DUEL_APP_ID, DuelApp::SLEEP_INDEX}},
    {{SHOOTOUT_ABORTED, 0}, {DUEL_APP_ID, DuelApp::IDLE_INDEX}},
    {{SYMBOL, 0}, {DUEL_APP_ID, DuelApp::IDLE_INDEX}},
    {{SYMBOL_MATCHED, 1}, {DUEL_APP_ID, DuelApp::IDLE_INDEX}},
};

}  // namespace quickdraw_state_graph_expectations

// Holds the four apps, populated but never mounted. Each app owns the states it
// registered and frees them; this only owns the apps.
struct QuickdrawAppsForTest {
    PlayerRegistrationApp* playerRegistration = nullptr;
    DuelApp* duel = nullptr;
    ShootoutApp* shootout = nullptr;
    SymbolApp* symbol = nullptr;

    /// Constructs and populates the four apps without mounting any of them.
    QuickdrawAppsForTest() {
        GameContext context;
        playerRegistration = new PlayerRegistrationApp(nullptr, nullptr, nullptr, nullptr);
        duel = new DuelApp(context);
        shootout = new ShootoutApp(context);
        symbol = new SymbolApp(context);
        playerRegistration->populateStateMap();
        duel->populateStateMap();
        shootout->populateStateMap();
        symbol->populateStateMap();
    }

    /// Frees the apps, and with them every state they registered.
    ~QuickdrawAppsForTest() {
        delete playerRegistration;
        delete duel;
        delete shootout;
        delete symbol;
    }

    /// The gameplay apps' states in the order the pre-split flat map registered
    /// them: duel, then shootout, then symbol.
    std::vector<State*> gameplayStates() const {
        std::vector<State*> states;
        for (const StateMachine* app : {static_cast<const StateMachine*>(duel),
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
    /// directly, a hand-off names an app plus a slot in that app's state map.
    State* resolveTarget(const StateTransition* edge) const {
        if (edge->getNextState() != nullptr) return edge->getNextState();
        const StateMachine* target = nullptr;
        if (edge->getTargetAppId().id == DUEL_APP_ID) target = duel;
        if (edge->getTargetAppId().id == SHOOTOUT_APP_ID) target = shootout;
        if (edge->getTargetAppId().id == SYMBOL_APP_ID) target = symbol;
        if (target == nullptr) return nullptr;
        int slot = edge->getEntryStateIndex();
        if (slot < 0 || slot >= static_cast<int>(target->getStateMap().size())) return nullptr;
        return target->getStateMap()[slot];
    }
};

// The concatenated app maps are the pre-split registration order. Each app's
// stateMap[0] is what a mount enters by default, and the entry-slot constants the
// cross-app edges use address these positions.
inline void quickdrawAppsRegisterStatesInPreSplitOrder() {
    QuickdrawAppsForTest apps;

    const std::vector<int>& expected =
        quickdraw_state_graph_expectations::PRE_SPLIT_GAMEPLAY_ORDER;
    std::vector<State*> actual = apps.gameplayStates();
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i]->getStateId(), expected[i])
            << "registration slot " << i << " changed";
    }

    // The slots the cross-app edges name, spelled out so a reorder that keeps the
    // ids in place but moves a constant is still caught.
    EXPECT_EQ(apps.duel->getStateMap()[DuelApp::AWAKEN_SEQUENCE_INDEX]->getStateId(), AWAKEN_SEQUENCE);
    EXPECT_EQ(apps.duel->getStateMap()[DuelApp::IDLE_INDEX]->getStateId(), IDLE);
    EXPECT_EQ(apps.duel->getStateMap()[DuelApp::DUEL_COUNTDOWN_INDEX]->getStateId(), DUEL_COUNTDOWN);
    EXPECT_EQ(apps.duel->getStateMap()[DuelApp::SLEEP_INDEX]->getStateId(), SLEEP);
    EXPECT_EQ(apps.shootout->getStateMap()[ShootoutApp::PROPOSAL_INDEX]->getStateId(), SHOOTOUT_PROPOSAL);
    EXPECT_EQ(apps.shootout->getStateMap()[ShootoutApp::SPECTATOR_INDEX]->getStateId(), SHOOTOUT_SPECTATOR);
    EXPECT_EQ(apps.shootout->getStateMap()[ShootoutApp::ELIMINATED_INDEX]->getStateId(), SHOOTOUT_ELIMINATED);
    EXPECT_EQ(apps.shootout->getStateMap()[ShootoutApp::ABORTED_INDEX]->getStateId(), SHOOTOUT_ABORTED);
    EXPECT_EQ(apps.symbol->getStateMap()[SymbolApp::SYMBOL_INDEX]->getStateId(), SYMBOL);
    EXPECT_EQ(apps.playerRegistration->getStateMap()[PlayerRegistrationApp::FETCH_USER_DATA_INDEX]->getStateId(),
              PlayerRegistrationStateId::FETCH_USER_DATA);
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

// Which of those edges became hand-offs, and the app plus entry slot each names.
// A hand-off that regressed to entry slot 0 would still resolve to a state and
// pass the equivalence check above only if slot 0 happened to be the right one,
// so the slots are pinned here directly.
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
        EXPECT_EQ(edge->getEntryStateIndex(), expected.second.second)
            << "state " << sourceId << " edge " << position << " changed entry slot";
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
    EXPECT_EQ(edge->getTargetAppId().id, DUEL_APP_ID);
    EXPECT_EQ(edge->getEntryStateIndex(), DuelApp::AWAKEN_SEQUENCE_INDEX);
    EXPECT_EQ(apps.duel->getStateMap()[edge->getEntryStateIndex()]->getStateId(), AWAKEN_SEQUENCE);
}
