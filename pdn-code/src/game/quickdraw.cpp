#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN): StateMachine(PDN) {
    this->player = player;
    PDN->setActiveComms(player->isHunter() ? OUTPUT_JACK : INPUT_JACK);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {

    Sleep* dormant = new Sleep(player->isHunter(), 0);
    AwakenSequence* activationSequence = new AwakenSequence();
    Idle* activated = new Idle(player->isHunter());
    Handshake* handshake = new Handshake(player);
    ConnectionSuccessful* duelAlert = new ConnectionSuccessful(player);
    DuelCountdown* duelCountdown = new DuelCountdown();
    Duel* duel = new Duel();
    Win* win = new Win(player);
    Lose* lose = new Lose(player);

    dormant->addTransition(
        new StateTransition(
            std::bind(&Sleep::transitionToAwakenSequence,
                dormant),
                activationSequence));
    activationSequence->addTransition(
        new StateTransition(
            std::bind(&AwakenSequence::transitionToIdle,
                activationSequence),
                activated));
    activated->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToHandshake,
                activated),
            handshake));
    handshake->addTransition(
        new StateTransition(
            std::bind(&Handshake::transitionToConnectionSuccessful,
                handshake),
                duelAlert));
    handshake->addTransition(
        new StateTransition(
            std::bind(&Handshake::transitionToIdle,
            handshake),
            activated));
    duelAlert->addTransition(
        new StateTransition(
            std::bind(&ConnectionSuccessful::transitionToCountdown,
                duelAlert),
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
                activated));
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
            dormant));
    lose->addTransition(
        new StateTransition(
            std::bind(&Lose::resetGame,
                lose),
                dormant));

    stateMap.push_back(dormant);
    stateMap.push_back(activationSequence);
    stateMap.push_back(activated);
    stateMap.push_back(handshake);
    stateMap.push_back(duelAlert);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

