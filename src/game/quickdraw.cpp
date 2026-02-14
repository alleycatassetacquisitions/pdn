#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager): StateMachine(QUICKDRAW_APP_ID) {
    this->player = player;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    this->remoteDebugManager = remoteDebugManager;
    this->wirelessManager = PDN->getWirelessManager();
    this->matchManager = new MatchManager();
    this->storageManager = PDN->getStorage();
    this->peerComms = PDN->getPeerComms();
    this->progressManager = new ProgressManager();
    this->progressManager->initialize(player, storageManager);
    this->fdnResultManager = new FdnResultManager();
    this->fdnResultManager->initialize(storageManager);
    PDN->setActiveComms(player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    quickdrawWirelessManager = nullptr;
    matchManager = nullptr;
    storageManager = nullptr;
    peerComms = nullptr;
    delete progressManager;
    progressManager = nullptr;
    delete fdnResultManager;
    fdnResultManager = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {
    matchManager->initialize(player, storageManager, peerComms, quickdrawWirelessManager);

    PlayerRegistration* playerRegistration = new PlayerRegistration(player, matchManager);
    FetchUserDataState* fetchUserData = new FetchUserDataState(player, wirelessManager, remoteDebugManager);
    WelcomeMessage* welcomeMessage = new WelcomeMessage(player);
    ConfirmOfflineState* confirmOffline = new ConfirmOfflineState(player);
    ChooseRoleState* chooseRole = new ChooseRoleState(player);
    AllegiancePickerState* allegiancePicker = new AllegiancePickerState(player);
    
    AwakenSequence* awakenSequence = new AwakenSequence(player);
    Idle* idle = new Idle(player, matchManager, quickdrawWirelessManager, progressManager);
    
    HandshakeInitiateState* handshakeInitiate = new HandshakeInitiateState(player);
    BountySendConnectionConfirmedState* bountySendCC = new BountySendConnectionConfirmedState(player, matchManager, quickdrawWirelessManager);
    HunterSendIdState* hunterSendId = new HunterSendIdState(player, matchManager, quickdrawWirelessManager);

    ConnectionSuccessful* connectionSuccessful = new ConnectionSuccessful(player);
    
    DuelCountdown* duelCountdown = new DuelCountdown(player, matchManager);
    Duel* duel = new Duel(player, matchManager, quickdrawWirelessManager);
    DuelPushed* duelPushed = new DuelPushed(player, matchManager);
    DuelReceivedResult* duelReceivedResult = new DuelReceivedResult(player, matchManager, quickdrawWirelessManager);
    DuelResult* duelResult = new DuelResult(player, matchManager, quickdrawWirelessManager);
    
    Win* win = new Win(player);
    Lose* lose = new Lose(player);
    
    Sleep* sleep = new Sleep(player);
    UploadMatchesState* uploadMatches = new UploadMatchesState(player, wirelessManager, matchManager, fdnResultManager);

    FdnDetected* fdnDetected = new FdnDetected(player);
    FdnReencounter* fdnReencounter = new FdnReencounter(player);
    FdnComplete* fdnComplete = new FdnComplete(player, progressManager, fdnResultManager);
    ColorProfilePrompt* colorProfilePrompt = new ColorProfilePrompt(player, progressManager);
    ColorProfilePicker* colorProfilePicker = new ColorProfilePicker(player, progressManager);
    KonamiPuzzle* konamiPuzzle = new KonamiPuzzle(player);

    playerRegistration->addTransition(
        new StateTransition(
            std::bind(&PlayerRegistration::transitionToUserFetch, playerRegistration),
            fetchUserData));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToConfirmOffline, fetchUserData),
            confirmOffline));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToUploadMatches, fetchUserData),
            uploadMatches));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToWelcomeMessage, fetchUserData),
            welcomeMessage));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::transitionToPlayerRegistration, fetchUserData),
            playerRegistration));

    confirmOffline->addTransition(
        new StateTransition(
            std::bind(&ConfirmOfflineState::transitionToChooseRole, confirmOffline),
            chooseRole));

    confirmOffline->addTransition(
        new StateTransition(
            std::bind(&ConfirmOfflineState::transitionToPlayerRegistration, confirmOffline),
            playerRegistration));

    chooseRole->addTransition(
        new StateTransition(
            std::bind(&ChooseRoleState::transitionToAllegiancePicker, chooseRole),
            allegiancePicker));

    allegiancePicker->addTransition(
        new StateTransition(
            std::bind(&AllegiancePickerState::transitionToWelcomeMessage, allegiancePicker),
            welcomeMessage));

    welcomeMessage->addTransition(
        new StateTransition(
            std::bind(&WelcomeMessage::transitionToGameplay, welcomeMessage),
            awakenSequence));

    awakenSequence->addTransition(
        new StateTransition(
            std::bind(&AwakenSequence::transitionToIdle, awakenSequence),
            idle));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToColorPicker, idle),
            colorProfilePicker));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::isFdnDetected, idle),
            fdnDetected));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToHandshake, idle),
            handshakeInitiate));

    fdnDetected->addTransition(
        new StateTransition(
            std::bind(&FdnDetected::transitionToKonamiPuzzle, fdnDetected),
            konamiPuzzle));

    fdnDetected->addTransition(
        new StateTransition(
            std::bind(&FdnDetected::transitionToReencounter, fdnDetected),
            fdnReencounter));

    fdnDetected->addTransition(
        new StateTransition(
            std::bind(&FdnDetected::transitionToFdnComplete, fdnDetected),
            fdnComplete));

    fdnDetected->addTransition(
        new StateTransition(
            std::bind(&FdnDetected::transitionToIdle, fdnDetected),
            idle));

    fdnReencounter->addTransition(
        new StateTransition(
            std::bind(&FdnReencounter::transitionToFdnComplete, fdnReencounter),
            fdnComplete));

    fdnReencounter->addTransition(
        new StateTransition(
            std::bind(&FdnReencounter::transitionToIdle, fdnReencounter),
            idle));

    fdnComplete->addTransition(
        new StateTransition(
            std::bind(&FdnComplete::transitionToColorPrompt, fdnComplete),
            colorProfilePrompt));

    fdnComplete->addTransition(
        new StateTransition(
            std::bind(&FdnComplete::transitionToIdle, fdnComplete),
            idle));

    colorProfilePrompt->addTransition(
        new StateTransition(
            std::bind(&ColorProfilePrompt::transitionToIdle, colorProfilePrompt),
            idle));

    colorProfilePicker->addTransition(
        new StateTransition(
            std::bind(&ColorProfilePicker::transitionToIdle, colorProfilePicker),
            idle));

    konamiPuzzle->addTransition(
        new StateTransition(
            std::bind(&KonamiPuzzle::transitionToIdle, konamiPuzzle),
            idle));

    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToBountySendCC, handshakeInitiate),
            bountySendCC));
    
    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToHunterSendId, handshakeInitiate),
            hunterSendId));
    
    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle, handshakeInitiate),
            idle));
    
    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BountySendConnectionConfirmedState::transitionToConnectionSuccessful, bountySendCC),
            connectionSuccessful));
    
    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle, bountySendCC),
            idle));
    
    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&HunterSendIdState::transitionToConnectionSuccessful, hunterSendId),
            connectionSuccessful));
    
    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle, hunterSendId),
            idle));

    connectionSuccessful->addTransition(
        new StateTransition(
            std::bind(&ConnectionSuccessful::transitionToCountdown, connectionSuccessful),
            duelCountdown));

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

    uploadMatches->addTransition(
        new StateTransition(
            std::bind(&UploadMatchesState::transitionToPlayerRegistration, uploadMatches),
            playerRegistration));

    sleep->addTransition(
        new StateTransition(
            std::bind(&Sleep::transitionToAwakenSequence, sleep),
            awakenSequence));

    stateMap.push_back(playerRegistration);
    stateMap.push_back(fetchUserData);
    stateMap.push_back(confirmOffline);
    stateMap.push_back(chooseRole);
    stateMap.push_back(allegiancePicker);
    stateMap.push_back(welcomeMessage);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    stateMap.push_back(handshakeInitiate);
    stateMap.push_back(bountySendCC);
    stateMap.push_back(hunterSendId);
    stateMap.push_back(connectionSuccessful);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(duelPushed);
    stateMap.push_back(duelReceivedResult);
    stateMap.push_back(duelResult);
    stateMap.push_back(win);
    stateMap.push_back(lose);
    stateMap.push_back(uploadMatches);
    stateMap.push_back(sleep);
    stateMap.push_back(fdnDetected);
    stateMap.push_back(fdnReencounter);
    stateMap.push_back(fdnComplete);
    stateMap.push_back(colorProfilePrompt);
    stateMap.push_back(colorProfilePicker);
    stateMap.push_back(konamiPuzzle);
}

Image Quickdraw::getImageForAllegiance(Allegiance allegiance, ImageType whichImage) {
    return getCollectionForAllegiance(allegiance)->at(whichImage);
}
