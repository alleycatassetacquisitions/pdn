#include "game/quickdraw-apps.hpp"

namespace {
// The abort rule, as an edge condition. Ten states carry an abort edge — the hub's
// Idle, the five interruptible match states and four of the shootout states — and
// all ten read this, so a split across three state maps cannot fork it.
std::function<bool()> tournamentAbortedCondition(ShootoutManager* shootoutManager) {
    return [shootoutManager]() {
        return shootoutManager && shootoutManager->getPhase() == ShootoutManager::Phase::ABORTED;
    };
}

// The abandoned-match rule the three interruptible match states share, in the same
// shape as the abort rule above so neither can fork.
std::function<bool()> matchAbandonedCondition(ConnectState<PDN>& matchState,
                                              ShootoutManager* shootoutManager) {
    return
    [&matchState, shootoutManager]() { return matchReturnsToIdle(matchState, shootoutManager); };
}
}  // namespace

HubApp::HubApp(const GameContext& context)
    : StateMachine(HUB_APP_ID)
    , context(context) {}

void HubApp::populateStateMap() {
    AwakenSequence* awakenSequence = new AwakenSequence(context);
    Idle* idle = new Idle(context);
    SupporterReady* supporterReady = new SupporterReady(context);
    Sleep* sleep = new Sleep(context);

    ShootoutManager* shootoutManager = context.shootoutManager;

    awakenSequence->addTransition(
        [awakenSequence]() { return awakenSequence->transitionToIdle(); },
        idle);

    // Auto-trigger Shootout on ring closure — the coordinator's own RDC event,
    // every other member's RING_CLOSED. Priority over MatchCountdown/
    // SupporterReady: in a closed ring, adjacent H-B pairs otherwise look like a
    // normal match initiation, and the ring intent (tournament) would be silently
    // demoted to a 1v1 match.
    idle->addAppTransition(
        [shootoutManager]() { return shootoutManager && shootoutManager->shouldEnterProposal(); },
        StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_PROPOSAL));

    idle->addAppTransition([idle]() { return idle->transitionToMatchCountdown(); },
                           StateId(MATCH_APP_ID), StateId(MATCH_COUNTDOWN));

    idle->addTransition(
        [idle]() { return idle->transitionToSupporterReady(); },
        supporterReady);

    idle->addAppTransition(tournamentAbortedCondition(shootoutManager),
                           StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ABORTED));

    idle->addAppTransition(
        [idle]() { return idle->transitionToSymbol(); },
        StateId(SYMBOL_APP_ID), StateId(SYMBOL));

    supporterReady->addTransition(
        [supporterReady]() { return supporterReady->transitionToIdle(); },
        idle);

    sleep->addTransition(
        [sleep]() { return sleep->transitionToAwakenSequence(); },
        awakenSequence);

    // First registered is the state a mount with no named entry enters at.
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    stateMap.push_back(supporterReady);
    stateMap.push_back(sleep);
}

MatchApp::MatchApp(const GameContext& context)
    : StateMachine(MATCH_APP_ID)
    , context(context) {}

void MatchApp::onStateDismounted(Device* device) {
    StateMachine::onStateDismounted(device);
    device->getHaptics()->off();
    context.matchManager->clearCurrentMatch();
}

void MatchApp::populateStateMap() {
    MatchCountdown* matchCountdown = new MatchCountdown(context);
    MatchState* matchState = new MatchState(context);
    MatchPushed* matchPushed = new MatchPushed(context);
    MatchReceivedResult* matchReceivedResult = new MatchReceivedResult(context);
    MatchResult* matchResult = new MatchResult(context);
    Win* win = new Win(context);
    Lose* lose = new Lose(context);
    UploadMatchesState* uploadMatches = new UploadMatchesState(context);

    ShootoutManager* shootoutManager = context.shootoutManager;

    std::function<bool()> phaseIsAborted = tournamentAbortedCondition(shootoutManager);

    matchCountdown->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID),
                                     StateId(SHOOTOUT_ABORTED));

    matchCountdown->addTransition([matchCountdown]() { return matchCountdown->shallWeBattle(); },
                                  matchState);

    matchCountdown->addAppTransition(matchAbandonedCondition(*matchCountdown, shootoutManager),
                                     StateId(HUB_APP_ID), StateId(IDLE));

    matchState->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID),
                                 StateId(SHOOTOUT_ABORTED));

    matchState->addAppTransition(
    [matchState]() { return matchState->transitionToShootoutSpectator(); },
    StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_SPECTATOR));

    matchState->addAppTransition(
    [matchState]() { return matchState->transitionToShootoutEliminated(); },
    StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ELIMINATED));

    matchState->addAppTransition([matchState]() { return matchState->transitionToIdle(); },
                                 StateId(HUB_APP_ID), StateId(IDLE));

    matchState->addTransition(
    [matchState]() { return matchState->transitionToMatchReceivedResult(); }, matchReceivedResult);

    matchState->addTransition([matchState]() { return matchState->transitionToMatchPushed(); },
                              matchPushed);

    matchPushed->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID),
                                  StateId(SHOOTOUT_ABORTED));

    matchPushed->addAppTransition(matchAbandonedCondition(*matchPushed, shootoutManager),
                                  StateId(HUB_APP_ID), StateId(IDLE));

    matchPushed->addTransition([matchPushed]() { return matchPushed->transitionToMatchResult(); },
                               matchResult);

    matchReceivedResult->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID),
                                          StateId(SHOOTOUT_ABORTED));

    matchReceivedResult->addAppTransition(
    matchAbandonedCondition(*matchReceivedResult, shootoutManager), StateId(HUB_APP_ID),
    StateId(IDLE));

    matchReceivedResult->addTransition(
    [matchReceivedResult]() { return matchReceivedResult->transitionToMatchResult(); },
    matchResult);

    matchResult->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID),
                                  StateId(SHOOTOUT_ABORTED));

    matchResult->addTransition([matchResult]() { return matchResult->transitionToWin(); }, win);

    matchResult->addTransition([matchResult]() { return matchResult->transitionToLose(); }, lose);

    matchResult->addAppTransition(
    [matchResult]() { return matchResult->transitionToShootoutSpectator(); },
    StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_SPECTATOR));

    matchResult->addAppTransition(
    [matchResult]() { return matchResult->transitionToShootoutEliminated(); },
    StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ELIMINATED));

    win->addTransition(
        [win]() { return win->resetGame(); },
        uploadMatches);

    lose->addTransition(
        [lose]() { return lose->resetGame(); },
        uploadMatches);

    uploadMatches->addAppTransition(
        [uploadMatches]() { return uploadMatches->transitionToSleep(); },
        StateId(HUB_APP_ID), StateId(SLEEP));

    stateMap.push_back(matchCountdown);
    stateMap.push_back(matchState);
    stateMap.push_back(matchPushed);
    stateMap.push_back(matchReceivedResult);
    stateMap.push_back(matchResult);
    stateMap.push_back(win);
    stateMap.push_back(lose);
    stateMap.push_back(uploadMatches);
}

ShootoutApp::ShootoutApp(const GameContext& context)
    : StateMachine(SHOOTOUT_APP_ID)
    , context(context) {}

void ShootoutApp::populateStateMap() {
    ShootoutProposal* proposal = new ShootoutProposal(context);
    ShootoutBracketReveal* bracketReveal = new ShootoutBracketReveal(context);
    ShootoutSpectator* spectator = new ShootoutSpectator(context);
    ShootoutEliminated* eliminated = new ShootoutEliminated(context);
    ShootoutFinalStandings* finalStandings = new ShootoutFinalStandings(context);
    ShootoutAborted* aborted = new ShootoutAborted(context);

    std::function<bool()> phaseIsAborted = tournamentAbortedCondition(context.shootoutManager);

    proposal->addTransition(
        [proposal]() { return proposal->transitionToBracketReveal(); },
        bracketReveal);
    proposal->addTransition(phaseIsAborted, aborted);

    bracketReveal->addAppTransition(
    [bracketReveal]() { return bracketReveal->transitionToMatchCountdown(); },
    StateId(MATCH_APP_ID), StateId(MATCH_COUNTDOWN));
    bracketReveal->addTransition(
        [bracketReveal]() { return bracketReveal->transitionToSpectator(); },
        spectator);
    bracketReveal->addTransition(phaseIsAborted, aborted);

    spectator->addAppTransition([spectator]() { return spectator->transitionToMatchCountdown(); },
                                StateId(MATCH_APP_ID), StateId(MATCH_COUNTDOWN));
    spectator->addTransition(
        [spectator]() { return spectator->transitionToFinalStandings(); },
        finalStandings);
    spectator->addTransition(phaseIsAborted, aborted);

    eliminated->addTransition(
        [eliminated]() { return eliminated->transitionToFinalStandings(); },
        finalStandings);
    eliminated->addTransition(phaseIsAborted, aborted);

    // Cable-event reset after TOURNAMENT_END: when the physical ring opens,
    // route through Sleep so the cooldown period elapses before the next
    // proposal can fire. Going straight to Idle let stale RDC chain state
    // (still advertising the old ring) feed back into the ring check on the
    // same loop tick as unplug, triggering a phantom Proposal with no peers
    // left to confirm.
    finalStandings->addAppTransition(
        [finalStandings]() { return finalStandings->transitionToSleep(); },
        StateId(HUB_APP_ID), StateId(SLEEP));

    aborted->addAppTransition(
        [aborted]() { return aborted->transitionToIdle(); },
        StateId(HUB_APP_ID), StateId(IDLE));

    stateMap.push_back(proposal);
    stateMap.push_back(bracketReveal);
    stateMap.push_back(spectator);
    stateMap.push_back(eliminated);
    stateMap.push_back(finalStandings);
    stateMap.push_back(aborted);
}

SymbolApp::SymbolApp(const GameContext& context)
    : StateMachine(SYMBOL_APP_ID)
    , context(context) {}

void SymbolApp::populateStateMap() {
    SymbolState* symbol = new SymbolState(context);
    SymbolMatched* symbolMatched = new SymbolMatched(context);

    symbol->addAppTransition(
        [symbol]() { return symbol->transitionToIdle(); },
        StateId(HUB_APP_ID), StateId(IDLE));

    symbol->addTransition(
        [symbol]() { return symbol->transitionToSymbolMatched(); },
        symbolMatched);

    symbolMatched->addTransition(
        [symbolMatched]() { return symbolMatched->transitionToSymbol(); },
        symbol);

    symbolMatched->addAppTransition(
        [symbolMatched]() { return symbolMatched->transitionToIdle(); },
        StateId(HUB_APP_ID), StateId(IDLE));

    stateMap.push_back(symbol);
    stateMap.push_back(symbolMatched);
}

AppConfig makeGameplayApps(const GameContext& context) {
    return {
    {StateId(HUB_APP_ID), new HubApp(context)},
    {StateId(MATCH_APP_ID), new MatchApp(context)},
    {StateId(SHOOTOUT_APP_ID), new ShootoutApp(context)},
    {StateId(SYMBOL_APP_ID), new SymbolApp(context)},
    };
}
