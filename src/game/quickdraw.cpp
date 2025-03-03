#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN): StateMachine(PDN) {
    this->player = player;
    PDN->setActiveComms(player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {

    Sleep* sleep = new Sleep(player);
    AwakenSequence* awakenSequence = new AwakenSequence();
    Idle* idle = new Idle(player);
    
    // New handshake states
    HandshakeInitiate* handshakeInitiate = new HandshakeInitiate(player);
    BountySendCC* bountySendCC = new BountySendCC(player);
    BountySendAck* bountySendAck = new BountySendAck(player);
    HunterSendId* hunterSendId = new HunterSendId(player);
    HunterSendAck* hunterSendAck = new HunterSendAck(player);
    HandshakeStartingLine* handshakeStartingLine = new HandshakeStartingLine(player);
    
    ConnectionSuccessful* connectionSuccessful = new ConnectionSuccessful(player);
    DuelCountdown* duelCountdown = new DuelCountdown(player);
    Duel* duel = new Duel(player);
    Win* win = new Win(player);
    Lose* lose = new Lose(player);

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
            std::bind(&HandshakeInitiate::transitionToBountySendCC,
                handshakeInitiate),
            bountySendCC));
    
    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiate::transitionToHunterSendId,
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
            std::bind(&BountySendCC::transitionToBountySendAck,
                bountySendCC),
            bountySendAck));
    
    // Common timeout transition for all handshake states
    bountySendCC->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                bountySendCC),
            idle));
    
    bountySendAck->addTransition(
        new StateTransition(
            std::bind(&BountySendAck::transitionToHandshakeStartingLine,
                bountySendAck),
            handshakeStartingLine));
    
    // Common timeout transition for all handshake states
    bountySendAck->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                bountySendAck),
            idle));
    
    // Hunter path
    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&HunterSendId::transitionToHunterSendAck,
                hunterSendId),
            hunterSendAck));
    
    // Common timeout transition for all handshake states
    hunterSendId->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                hunterSendId),
            idle));
    
    hunterSendAck->addTransition(
        new StateTransition(
            std::bind(&HunterSendAck::transitionToHandshakeStartingLine,
                hunterSendAck),
            handshakeStartingLine));
    
    // Common timeout transition for all handshake states
    hunterSendAck->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                hunterSendAck),
            idle));
    
    // Starting line state
    handshakeStartingLine->addTransition(
        new StateTransition(
            std::bind(&HandshakeStartingLine::transitionToConnectionSuccessful,
                handshakeStartingLine),
            connectionSuccessful));
    
    // Common timeout transition for all handshake states
    handshakeStartingLine->addTransition(
        new StateTransition(
            std::bind(&BaseHandshakeState::transitionToIdle,
                handshakeStartingLine),
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

    stateMap.push_back(sleep);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    
    // Add new handshake states
    stateMap.push_back(handshakeInitiate);
    stateMap.push_back(bountySendCC);
    stateMap.push_back(bountySendAck);
    stateMap.push_back(hunterSendId);
    stateMap.push_back(hunterSendAck);
    stateMap.push_back(handshakeStartingLine);
    
    stateMap.push_back(connectionSuccessful);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

Image Quickdraw::getImageForAllegiance(Allegiance allegiance, ImageType whichImage) {
    return getCollectionForAllegiance(allegiance)->at(whichImage);
}