#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
//
// Created by Elli Furedy on 9/30/2024.
//
/*
if (peekGameComms() == ZAP) {
    readGameComms();
    writeGameComms(YOU_DEFEATED_ME);
    captured = true;
    return;
  } else if (peekGameComms() == YOU_DEFEATED_ME) {
    readGameComms();
    wonBattle = true;
    return;
  } else {
    readGameComms();
  }

  // primary.tick();

  if (startDuelTimer) {
    setTimer(DUEL_TIMEOUT);
    startDuelTimer = false;
  }

  if (timerExpired()) {
    // FastLED.setBrightness(0);
    bvbDuel = false;
    duelTimedOut = true;
  }
 */
Duel::Duel(Player* player) : State(DUEL) {
    this->player = player;

    std::vector<const string *> reading;

    reading.push_back(&ZAP);
    reading.push_back(&YOU_DEFEATED_ME);
}

Duel::~Duel() {
    this->player = nullptr;
}

void Duel::onStateMounted(Device *PDN) {
    PDN->setButtonClick(
        ButtonInteraction::PRESS,
        ButtonIdentifier::PRIMARY_BUTTON,
        [](void *PDN) {
            ButtonPress(static_cast<Device *>(PDN));
        },
        PDN);

    PDN->setButtonClick(
        ButtonInteraction::PRESS,
        ButtonIdentifier::SECONDARY_BUTTON,
        [](void *PDN) {
            ButtonPress(static_cast<Device *>(PDN));
        },
        PDN);

    duelTimer.setTimer(DUEL_TIMEOUT);

    PDN->invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::DRAW))->
    render();
}

void Duel::onStateLoop(Device *PDN) {

    string *validMessage = waitForValidMessage(PDN);
    if (validMessage != nullptr) {
        if (*validMessage == ZAP) {
            PDN->writeString(&YOU_DEFEATED_ME);
            captured = true;
        } else if (*validMessage == YOU_DEFEATED_ME) {
            wonBattle = true;
        }
    }
}

bool Duel::transitionToActivated() {
    return duelTimer.expired();
}

bool Duel::transitionToWin() {
    return wonBattle;
}

bool Duel::transitionToLose() {
    return captured;
}

void Duel::onStateDismounted(Device *PDN) {
    duelTimer.invalidate();
    wonBattle = false;
    captured = false;
}
