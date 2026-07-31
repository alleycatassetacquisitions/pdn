#include "game/quickdraw-apps.hpp"

DuelApp::DuelApp(const GameContext& context)
    : StateMachine(DUEL_APP_ID)
    , context(context) {}

void DuelApp::populateStateMap() {
    AwakenSequence* awakenSequence = new AwakenSequence(context);
    Idle* idle = new Idle(context);
    SupporterReady* supporterReady = new SupporterReady(context);
    DuelCountdown* duelCountdown = new DuelCountdown(context);
    Duel* duel = new Duel(context);
    DuelPushed* duelPushed = new DuelPushed(context);
    DuelReceivedResult* duelReceivedResult = new DuelReceivedResult(context);
    DuelResult* duelResult = new DuelResult(context);
    Win* win = new Win(context);
    Lose* lose = new Lose(context);
    UploadMatchesState* uploadMatches = new UploadMatchesState(context);
    Sleep* sleep = new Sleep(context);

    ShootoutManager* shootoutManager = context.shootoutManager;

    // Held once and reused by the six states a live tournament can interrupt;
    // re-spelling it per state would fork the abort rule.
    std::function<bool()> phaseIsAborted = [shootoutManager]() {
        return shootoutManager && shootoutManager->getPhase() == ShootoutManager::Phase::ABORTED;
    };

    awakenSequence->addTransition(
        new StateTransition(
            [awakenSequence]() { return awakenSequence->transitionToIdle(); },
            idle));

    // Auto-trigger Shootout on ring closure — the coordinator's own RDC event,
    // every other member's RING_CLOSED. Priority over DuelCountdown/
    // SupporterReady: in a closed ring, adjacent H-B pairs otherwise look like a
    // normal duel initiation, and the ring intent (tournament) would be silently
    // demoted to a 1v1 duel.
    idle->addAppTransition(
        [shootoutManager]() { return shootoutManager && shootoutManager->shouldEnterProposal(); },
        StateId(SHOOTOUT_APP_ID), ShootoutApp::PROPOSAL_INDEX);

    idle->addTransition(
        new StateTransition(
            [idle]() { return idle->transitionToDuelCountdown(); },
            duelCountdown));

    idle->addTransition(
        new StateTransition(
            [idle]() { return idle->transitionToSupporterReady(); },
            supporterReady));

    idle->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), ShootoutApp::ABORTED_INDEX);

    idle->addAppTransition(
        [idle]() { return idle->transitionToSymbol(); },
        StateId(SYMBOL_APP_ID), SymbolApp::SYMBOL_INDEX);

    supporterReady->addTransition(
        new StateTransition(
            [supporterReady]() { return supporterReady->transitionToIdle(); },
            idle));

    duelCountdown->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), ShootoutApp::ABORTED_INDEX);

    duelCountdown->addTransition(
        new StateTransition(
            [duelCountdown]() { return duelCountdown->shallWeBattle(); },
            duel));

    duelCountdown->addTransition(
        new StateTransition(
            [duelCountdown, shootoutManager]() {
                return duelReturnsToIdle(*duelCountdown, shootoutManager);
            },
            idle));

    duel->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), ShootoutApp::ABORTED_INDEX);

    duel->addAppTransition(
        [duel]() { return duel->transitionToShootoutSpectator(); },
        StateId(SHOOTOUT_APP_ID), ShootoutApp::SPECTATOR_INDEX);

    duel->addAppTransition(
        [duel]() { return duel->transitionToShootoutEliminated(); },
        StateId(SHOOTOUT_APP_ID), ShootoutApp::ELIMINATED_INDEX);

    duel->addTransition(
        new StateTransition(
            [duel]() { return duel->transitionToIdle(); },
            idle));

    duel->addTransition(
        new StateTransition(
            [duel]() { return duel->transitionToDuelReceivedResult(); },
            duelReceivedResult));

    duel->addTransition(
        new StateTransition(
            [duel]() { return duel->transitionToDuelPushed(); },
            duelPushed));

    duelPushed->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), ShootoutApp::ABORTED_INDEX);

    duelPushed->addTransition(
        new StateTransition(
            [duelPushed, shootoutManager]() {
                return duelReturnsToIdle(*duelPushed, shootoutManager);
            },
            idle));

    duelPushed->addTransition(
        new StateTransition(
            [duelPushed]() { return duelPushed->transitionToDuelResult(); },
            duelResult));

    duelReceivedResult->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), ShootoutApp::ABORTED_INDEX);

    duelReceivedResult->addTransition(
        new StateTransition(
            [duelReceivedResult, shootoutManager]() {
                return duelReturnsToIdle(*duelReceivedResult, shootoutManager);
            },
            idle));

    duelReceivedResult->addTransition(
        new StateTransition(
            [duelReceivedResult]() { return duelReceivedResult->transitionToDuelResult(); },
            duelResult));

    duelResult->addAppTransition(phaseIsAborted, StateId(SHOOTOUT_APP_ID), ShootoutApp::ABORTED_INDEX);

    duelResult->addTransition(
        new StateTransition(
            [duelResult]() { return duelResult->transitionToWin(); },
            win));

    duelResult->addTransition(
        new StateTransition(
            [duelResult]() { return duelResult->transitionToLose(); },
            lose));

    duelResult->addAppTransition(
        [duelResult]() { return duelResult->transitionToShootoutSpectator(); },
        StateId(SHOOTOUT_APP_ID), ShootoutApp::SPECTATOR_INDEX);

    duelResult->addAppTransition(
        [duelResult]() { return duelResult->transitionToShootoutEliminated(); },
        StateId(SHOOTOUT_APP_ID), ShootoutApp::ELIMINATED_INDEX);

    win->addTransition(
        new StateTransition(
            [win]() { return win->resetGame(); },
            uploadMatches));

    lose->addTransition(
        new StateTransition(
            [lose]() { return lose->resetGame(); },
            uploadMatches));

    uploadMatches->addTransition(
        new StateTransition(
            [uploadMatches]() { return uploadMatches->transitionToSleep(); },
            sleep));

    sleep->addTransition(
        new StateTransition(
            [sleep]() { return sleep->transitionToAwakenSequence(); },
            awakenSequence));

    // Order matters: index 0 is the state a mount enters by default, and the
    // *_INDEX constants in the header address these slots.
    stateMap.push_back(awakenSequence);      // 0
    stateMap.push_back(idle);                // 1
    stateMap.push_back(supporterReady);      // 2
    stateMap.push_back(duelCountdown);       // 3
    stateMap.push_back(duel);                // 4
    stateMap.push_back(duelPushed);          // 5
    stateMap.push_back(duelReceivedResult);  // 6
    stateMap.push_back(duelResult);          // 7
    stateMap.push_back(win);                 // 8
    stateMap.push_back(lose);                // 9
    stateMap.push_back(uploadMatches);       // 10
    stateMap.push_back(sleep);               // 11
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

    proposal->addTransition(
        new StateTransition(
            [proposal]() { return proposal->transitionToBracketReveal(); },
            bracketReveal));
    proposal->addTransition(
        new StateTransition(
            [proposal]() { return proposal->transitionToAborted(); },
            aborted));

    bracketReveal->addAppTransition(
        [bracketReveal]() { return bracketReveal->transitionToDuelCountdown(); },
        StateId(DUEL_APP_ID), DuelApp::DUEL_COUNTDOWN_INDEX);
    bracketReveal->addTransition(
        new StateTransition(
            [bracketReveal]() { return bracketReveal->transitionToSpectator(); },
            spectator));
    bracketReveal->addTransition(
        new StateTransition(
            [bracketReveal]() { return bracketReveal->transitionToAborted(); },
            aborted));

    spectator->addAppTransition(
        [spectator]() { return spectator->transitionToDuelCountdown(); },
        StateId(DUEL_APP_ID), DuelApp::DUEL_COUNTDOWN_INDEX);
    spectator->addTransition(
        new StateTransition(
            [spectator]() { return spectator->transitionToFinalStandings(); },
            finalStandings));
    spectator->addTransition(
        new StateTransition(
            [spectator]() { return spectator->transitionToAborted(); },
            aborted));

    eliminated->addTransition(
        new StateTransition(
            [eliminated]() { return eliminated->transitionToFinalStandings(); },
            finalStandings));
    eliminated->addTransition(
        new StateTransition(
            [eliminated]() { return eliminated->transitionToAborted(); },
            aborted));

    // Cable-event reset after TOURNAMENT_END: when the physical ring opens,
    // route through Sleep so the cooldown period elapses before the next
    // proposal can fire. Going straight to Idle let stale RDC chain state
    // (still advertising the old ring) feed back into CDM::isLoop on the
    // same loop tick as unplug, triggering a phantom Proposal with no peers
    // left to confirm.
    finalStandings->addAppTransition(
        [finalStandings]() { return finalStandings->transitionToSleep(); },
        StateId(DUEL_APP_ID), DuelApp::SLEEP_INDEX);

    aborted->addAppTransition(
        [aborted]() { return aborted->transitionToIdle(); },
        StateId(DUEL_APP_ID), DuelApp::IDLE_INDEX);

    stateMap.push_back(proposal);        // 0
    stateMap.push_back(bracketReveal);   // 1
    stateMap.push_back(spectator);       // 2
    stateMap.push_back(eliminated);      // 3
    stateMap.push_back(finalStandings);  // 4
    stateMap.push_back(aborted);         // 5
}

SymbolApp::SymbolApp(const GameContext& context)
    : StateMachine(SYMBOL_APP_ID)
    , context(context) {}

void SymbolApp::populateStateMap() {
    SymbolState* symbol = new SymbolState(context);
    SymbolMatched* symbolMatched = new SymbolMatched(context);

    symbol->addAppTransition(
        [symbol]() { return symbol->transitionToIdle(); },
        StateId(DUEL_APP_ID), DuelApp::IDLE_INDEX);

    symbol->addTransition(
        new StateTransition(
            [symbol]() { return symbol->transitionToSymbolMatched(); },
            symbolMatched));

    symbolMatched->addTransition(
        new StateTransition(
            [symbolMatched]() { return symbolMatched->transitionToSymbol(); },
            symbol));

    symbolMatched->addAppTransition(
        [symbolMatched]() { return symbolMatched->transitionToIdle(); },
        StateId(DUEL_APP_ID), DuelApp::IDLE_INDEX);

    stateMap.push_back(symbol);         // 0
    stateMap.push_back(symbolMatched);  // 1
}
