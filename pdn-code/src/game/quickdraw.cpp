#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN): StateMachine(PDN) {
    this->player = player;
    Device::GetInstance()->setActiveComms(player->isHunter() ? OUTPUT_JACK : INPUT_JACK);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {

    Dormant* dormant = new Dormant(player->isHunter(), 0);
    ActivationSequence* activationSequence = new ActivationSequence();
    Activated* activated = new Activated(player->isHunter());
    Handshake* handshake = new Handshake(player);
    DuelAlert* duelAlert = new DuelAlert(player);
    DuelCountdown* duelCountdown = new DuelCountdown();
    Duel* duel = new Duel();
    Win* win = new Win(player);
    Lose* lose = new Lose(player);

    dormant->addTransition(
        new StateTransition(
            std::bind(&Dormant::transitionToActivationSequence,
                dormant),
                activationSequence));
    activationSequence->addTransition(
        new StateTransition(
            std::bind(&ActivationSequence::transitionToActivated,
                activationSequence),
                activated));
    activated->addTransition(
        new StateTransition(
            std::bind(&Activated::transitionToHandshake,
                activated),
            handshake));
    handshake->addTransition(
        new StateTransition(
            std::bind(&Handshake::transitionToDuelAlert,
                handshake),
                duelAlert));
    handshake->addTransition(
        new StateTransition(
            std::bind(&Handshake::transitionToActivated,
            handshake),
            activated));
    duelAlert->addTransition(
        new StateTransition(
            std::bind(&DuelAlert::transitionToCountdown,
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

