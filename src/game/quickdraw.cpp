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
    Handshake* handshake = new Handshake(player);
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
            handshake));

    handshake->addTransition(
        new StateTransition(
            std::bind(&Handshake::transitionToConnectionSuccessful,
                handshake),
                connectionSuccessful));
    handshake->addTransition(
        new StateTransition(
            std::bind(&Handshake::transitionToIdle,
            handshake),
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
    stateMap.push_back(handshake);
    stateMap.push_back(connectionSuccessful);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

Image Quickdraw::getImageForAllegiance(Allegiance allegiance, ImageType whichImage) {
    return getCollectionForAllegiance(allegiance)->at(whichImage);
}