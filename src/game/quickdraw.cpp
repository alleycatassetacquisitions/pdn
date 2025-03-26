#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN, WirelessManager* wirelessManager): StateMachine(PDN) {
    this->player = player;
    this->wirelessManager = wirelessManager;
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {

    PlayerRegistration* playerRegistration = new PlayerRegistration(player);
    FetchUserDataState* fetchUserData = new FetchUserDataState(player, wirelessManager);
    WelcomeMessage* welcomeMessage = new WelcomeMessage(player);
    ConfirmOfflineState* confirmOffline = new ConfirmOfflineState(player);
    ChooseRoleState* chooseRole = new ChooseRoleState(player);
    AllegiancePickerState* allegiancePicker = new AllegiancePickerState(player);
    
    AwakenSequence* awakenSequence = new AwakenSequence(player);
    // Wireless manager should be injected into the state here.
    Idle* idle = new Idle(player, wirelessManager);
    
    HandshakeInitiateState* handshakeInitiate = new HandshakeInitiateState(player);
    BountySendConnectionConfirmedState* bountySendCC = new BountySendConnectionConfirmedState(player);
    HunterSendIdState* hunterSendId = new HunterSendIdState(player);

    ConnectionSuccessful* connectionSuccessful = new ConnectionSuccessful(player);
    DuelCountdown* duelCountdown = new DuelCountdown(player);
    Duel* duel = new Duel(player);
    DuelResult* duelResult = new DuelResult(player);
    Win* win = new Win(player, wirelessManager);
    Lose* lose = new Lose(player, wirelessManager);

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
            std::bind(&FetchUserDataState::transitionToWelcomeMessage, fetchUserData),
            welcomeMessage));

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
            std::bind(&AwakenSequence::transitionToIdle,
                awakenSequence),
                idle));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToHandshake,
                idle),
            handshakeInitiate));

    // Handshake state transitions
    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToBountySendCC,
                handshakeInitiate),
            bountySendCC));
    
    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToHunterSendId,
                handshakeInitiate),
            hunterSendId));
    
    // Common timeout transition for all handshake states
    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                handshakeInitiate),
            idle));
    
    // Bounty path
    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BountySendConnectionConfirmedState::transitionToConnectionSuccessful,
                bountySendCC),
            connectionSuccessful));
    
    // Common timeout transition for all handshake states
    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                bountySendCC),
            idle));
    
    // Hunter path
    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&HunterSendIdState::transitionToConnectionSuccessful,
                hunterSendId),
            connectionSuccessful));
    
    // Common timeout transition for all handshake states
    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                hunterSendId),
            idle));

    connectionSuccessful->addTransition(
        new StateTransition(
            std::bind(&ConnectionSuccessful::transitionToCountdown,
                connectionSuccessful),
            duelCountdown));

    duelCountdown->addTransition(
        new StateTransition(
            std::bind(&DuelCountdown::shallWeBattle,
                duelCountdown),
                duel));
    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToIdle,
                duel),
                idle));
    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToDuelResult,
                duel),
                duelResult));
    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToWin,
                duelResult),
                win));
    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToLose,
                duelResult),
                lose));
    win->addTransition(
        new StateTransition(
        std::bind(&Win::resetGame,
            win),
            awakenSequence));
    lose->addTransition(
        new StateTransition(
            std::bind(&Lose::resetGame,
                lose),
                awakenSequence));

    stateMap.push_back(playerRegistration);
    stateMap.push_back(fetchUserData);
    stateMap.push_back(confirmOffline);
    stateMap.push_back(chooseRole);
    stateMap.push_back(allegiancePicker);
    stateMap.push_back(welcomeMessage);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    
    // Add new handshake states
    stateMap.push_back(handshakeInitiate);
    stateMap.push_back(bountySendCC);
    stateMap.push_back(hunterSendId);
    stateMap.push_back(connectionSuccessful);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(duelResult);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

Image Quickdraw::getImageForAllegiance(Allegiance allegiance, ImageType whichImage) {
    return getCollectionForAllegiance(allegiance)->at(whichImage);
}