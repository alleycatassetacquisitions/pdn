#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager): StateMachine(QUICKDRAW_APP_ID) {
    this->player = player;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    this->remoteDebugManager = remoteDebugManager;
    this->wirelessManager = PDN->getWirelessManager();
    this->matchManager = new MatchManager();
    this->storageManager = PDN->getStorage();
    this->peerComms = PDN->getPeerComms();
    PDN->getSerialManager()->setActiveComms(player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    quickdrawWirelessManager = nullptr;
    matchManager = nullptr;
    storageManager = nullptr;
    peerComms = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {
    matchManager->initialize(player, storageManager, peerComms, quickdrawWirelessManager);

    // Sub-state machines for player registration and handshake
    PlayerRegistrationApp* playerRegistration = new PlayerRegistrationApp(player, wirelessManager, matchManager, remoteDebugManager);
    HandshakeApp* handshake = new HandshakeApp(player, matchManager, quickdrawWirelessManager);

    // Quickdraw gameplay states
    AwakenSequence* awakenSequence = new AwakenSequence(player);
    Idle* idle = new Idle(player, matchManager, quickdrawWirelessManager);

    DuelCountdown* duelCountdown = new DuelCountdown(player, matchManager);
    Duel* duel = new Duel(player, matchManager, quickdrawWirelessManager);
    DuelPushed* duelPushed = new DuelPushed(player, matchManager);
    DuelReceivedResult* duelReceivedResult = new DuelReceivedResult(player, matchManager, quickdrawWirelessManager);
    DuelResult* duelResult = new DuelResult(player, matchManager, quickdrawWirelessManager);
    
    Win* win = new Win(player);
    Lose* lose = new Lose(player);
    
    Sleep* sleep = new Sleep(player);
    UploadMatchesState* uploadMatches = new UploadMatchesState(player, wirelessManager, matchManager);

    // --- Transitions from PlayerRegistration app ---
    playerRegistration->addTransition(
        new StateTransition(
            std::bind(&PlayerRegistrationApp::readyForGameplay, playerRegistration),
            awakenSequence));

    // --- Quickdraw gameplay transitions ---
    awakenSequence->addTransition(
        new StateTransition(
            std::bind(&AwakenSequence::transitionToIdle, awakenSequence),
            idle));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToHandshake, idle),
            handshake));

    // --- Transitions from Handshake app ---
    handshake->addTransition(
        new StateTransition(
            std::bind(&HandshakeApp::readyForCountdown, handshake),
            duelCountdown));

    handshake->addTransition(
        new StateTransition(
            std::bind(&HandshakeApp::transitionToIdle, handshake),
            idle));

    // --- Duel flow ---
    duelCountdown->addTransition(
        new StateTransition(
            std::bind(&DuelCountdown::shallWeBattle, duelCountdown),
            duel));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToIdle, duel),
            idle));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToDuelReceivedResult, duel),
            duelReceivedResult));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToDuelPushed, duel),
            duelPushed));

    duelPushed->addTransition(
        new StateTransition(
            std::bind(&DuelPushed::transitionToDuelResult, duelPushed),
            duelResult));

    duelReceivedResult->addTransition(
        new StateTransition(
            std::bind(&DuelReceivedResult::transitionToDuelResult, duelReceivedResult),
            duelResult));

    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToWin, duelResult),
            win));

    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToLose, duelResult),
            lose));

    // --- Post-game flow ---
    win->addTransition(
        new StateTransition(
            std::bind(&Win::resetGame, win),
            uploadMatches));

    lose->addTransition(
        new StateTransition(
            std::bind(&Lose::resetGame, lose),
            uploadMatches));

    uploadMatches->addTransition(
        new StateTransition(
            std::bind(&UploadMatchesState::transitionToSleep, uploadMatches),
            sleep));

    sleep->addTransition(
        new StateTransition(
            std::bind(&Sleep::transitionToAwakenSequence, sleep),
            awakenSequence));

    // State map - order matters: first entry is the initial state
    stateMap.push_back(playerRegistration);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    stateMap.push_back(handshake);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(duelPushed);
    stateMap.push_back(duelReceivedResult);
    stateMap.push_back(duelResult);
    stateMap.push_back(win);
    stateMap.push_back(lose);
    stateMap.push_back(uploadMatches);
    stateMap.push_back(sleep);
}
