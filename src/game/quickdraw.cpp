#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN, WirelessManager* wirelessManager): StateMachine(PDN) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    PDN->setActiveComms(player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {

    PlayerRegistration* playerRegistration = new PlayerRegistration(player);
    FetchUserDataState* fetchUserData = new FetchUserDataState(player, wirelessManager);
    Sleep* sleep = new Sleep(player);
    AwakenSequence* awakenSequence = new AwakenSequence();
    Idle* idle = new Idle(player);
    
    // New handshake states
    HandshakeInitiateState* handshakeInitiate = new HandshakeInitiateState(player);
    BountySendConnectionConfirmedState* bountySendCC = new BountySendConnectionConfirmedState(player);
    // BountySendFinalAckState* bountySendAck = new BountySendFinalAckState(player);
    HunterSendIdState* hunterSendId = new HunterSendIdState(player);
    // HunterSendFinalAckState* hunterSendAck = new HunterSendFinalAckState(player);
    // StartingLineState* startingLine = new StartingLineState(player);
    
    ConnectionSuccessful* connectionSuccessful = new ConnectionSuccessful(player);
    DuelCountdown* duelCountdown = new DuelCountdown(player);
    Duel* duel = new Duel(player);
    Win* win = new Win(player);
    Lose* lose = new Lose(player);

    playerRegistration->addTransition(
        new StateTransition(
            std::bind(&PlayerRegistration::transitionToUserFetch, playerRegistration),
            fetchUserData));

    fetchUserData->addTransition(
        new StateTransition(
            std::bind(&FetchUserDataState::resetToPlayerRegistration, fetchUserData),
            playerRegistration));

    sleep->addTransition(
        new StateTransition(
            /* condition function */std::bind(&Sleep::transitionToAwakenSequence, sleep),
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
            std::bind(&Duel::transitionToActivated,
                duel),
                idle));
    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToWin,
                duel),
                win));
    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToLose,
                duel),
                lose));
    win->addTransition(
        new StateTransition(
        std::bind(&Win::resetGame,
            win),
            sleep));
    lose->addTransition(
        new StateTransition(
            std::bind(&Lose::resetGame,
                lose),
                sleep));

    stateMap.push_back(playerRegistration);
    stateMap.push_back(fetchUserData);
    stateMap.push_back(sleep);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    
    // Add new handshake states
    stateMap.push_back(handshakeInitiate);
    stateMap.push_back(bountySendCC);
    // stateMap.push_back(bountySendAck);
    stateMap.push_back(hunterSendId);
    // stateMap.push_back(hunterSendAck);
    // stateMap.push_back(startingLine);
    
    stateMap.push_back(connectionSuccessful);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

Image Quickdraw::getImageForAllegiance(Allegiance allegiance, ImageType whichImage) {
    return getCollectionForAllegiance(allegiance)->at(whichImage);
}