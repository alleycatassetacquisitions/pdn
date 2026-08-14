#include "game/quickdraw-apps.hpp"

namespace {
// The abort rule, as an edge condition. Ten states carry an abort edge — the hub's
// Idle, the five interruptible duel states and four of the shootout states — and
// all ten read this, so a split across three state maps cannot fork it.
std::function<bool()> tournamentAbortedCondition(ShootoutManager* shootoutManager) {
    return [shootoutManager]() {
        return shootoutManager && shootoutManager->getPhase() == ShootoutManager::Phase::ABORTED;
    };
}

// The abandoned-duel rule the three interruptible duel states share, in the same
// shape as the abort rule above so neither can fork.
std::function<bool()> duelAbandonedCondition(ConnectState<PDN>& duel,
                                             ShootoutManager* shootoutManager) {
    return [&duel, shootoutManager]() { return duelReturnsToIdle(duel, shootoutManager); };
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
    // every other member's RING_CLOSED. Priority over DuelCountdown/
    // SupporterReady: in a closed ring, adjacent H-B pairs otherwise look like a
    // normal duel initiation, and the ring intent (tournament) would be silently
    // demoted to a 1v1 duel.
    idle->addAppTransition(
        [shootoutManager]() { return shootoutManager && shootoutManager->shouldEnterProposal(); },
        StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_PROPOSAL));

    idle->addAppTransition(
        [idle]() { return idle->transitionToDuelCountdown(); },
        StateId(DUEL_APP_ID), StateId(DUEL_COUNTDOWN));

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

DuelApp::DuelApp(const GameContext& context)
    : StateMachine(DUEL_APP_ID)
    , context(context) {}

void DuelApp::populateStateMap() {
    DuelCountdown* duelCountdown = new DuelCountdown(context);
    Duel* duel = new Duel(context);
    DuelPushed* duelPushed = new DuelPushed(context);
    DuelReceivedResult* duelReceivedResult = new DuelReceivedResult(context);
    DuelResult* duelResult = new DuelResult(context);
    Win* win = new Win(context);
    Lose* lose = new Lose(context);
    UploadMatchesState* uploadMatches = new UploadMatchesState(context);

    ShootoutManager* shootoutManager = context.shootoutManager;

    std::function<bool()> phaseIsAborted = tournamentAbortedCondition(shootoutManager);

    duelCountdown->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ABORTED));

    duelCountdown->addTransition(
        [duelCountdown]() { return duelCountdown->shallWeBattle(); },
        duel);

    duelCountdown->addAppTransition(
        duelAbandonedCondition(*duelCountdown, shootoutManager),
        StateId(HUB_APP_ID), StateId(IDLE));

    duel->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ABORTED));

    duel->addAppTransition(
        [duel]() { return duel->transitionToShootoutSpectator(); },
        StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_SPECTATOR));

    duel->addAppTransition(
        [duel]() { return duel->transitionToShootoutEliminated(); },
        StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ELIMINATED));

    duel->addAppTransition(
        [duel]() { return duel->transitionToIdle(); },
        StateId(HUB_APP_ID), StateId(IDLE));

    duel->addTransition(
        [duel]() { return duel->transitionToDuelReceivedResult(); },
        duelReceivedResult);

    duel->addTransition(
        [duel]() { return duel->transitionToDuelPushed(); },
        duelPushed);

    duelPushed->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ABORTED));

    duelPushed->addAppTransition(
        duelAbandonedCondition(*duelPushed, shootoutManager),
        StateId(HUB_APP_ID), StateId(IDLE));

    duelPushed->addTransition(
        [duelPushed]() { return duelPushed->transitionToDuelResult(); },
        duelResult);

    duelReceivedResult->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ABORTED));

    duelReceivedResult->addAppTransition(
        duelAbandonedCondition(*duelReceivedResult, shootoutManager),
        StateId(HUB_APP_ID), StateId(IDLE));

    duelReceivedResult->addTransition(
        [duelReceivedResult]() { return duelReceivedResult->transitionToDuelResult(); },
        duelResult);

    duelResult->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_ABORTED));

    duelResult->addTransition(
        [duelResult]() { return duelResult->transitionToWin(); },
        win);

    duelResult->addTransition(
        [duelResult]() { return duelResult->transitionToLose(); },
        lose);

    duelResult->addAppTransition(
        [duelResult]() { return duelResult->transitionToShootoutSpectator(); },
        StateId(SHOOTOUT_APP_ID), StateId(SHOOTOUT_SPECTATOR));

    duelResult->addAppTransition(
        [duelResult]() { return duelResult->transitionToShootoutEliminated(); },
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

    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(duelPushed);
    stateMap.push_back(duelReceivedResult);
    stateMap.push_back(duelResult);
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
        [bracketReveal]() { return bracketReveal->transitionToDuelCountdown(); },
        StateId(DUEL_APP_ID), StateId(DUEL_COUNTDOWN));
    bracketReveal->addTransition(
        [bracketReveal]() { return bracketReveal->transitionToSpectator(); },
        spectator);
    bracketReveal->addTransition(phaseIsAborted, aborted);

    spectator->addAppTransition(
        [spectator]() { return spectator->transitionToDuelCountdown(); },
        StateId(DUEL_APP_ID), StateId(DUEL_COUNTDOWN));
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
    // (still advertising the old ring) feed back into CDM::isLoop on the
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
