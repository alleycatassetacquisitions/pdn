//
// Created by Elli Furedy on 9/30/2024.
//
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include <string>
#include "device/device.hpp"

#define TAG "SleepState"

using namespace std;

Sleep::Sleep(const GameContext& ctx)
    : TypedState<PDN>(SLEEP) {
    this->player = ctx.player;
}

Sleep::~Sleep() {
    this->player = nullptr;
}

void Sleep::onStateMounted(PDN* pdn) {
    pdn->getDisplay()->
        invalidateScreen()->
        drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_LEFT))->
        drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::LOGO_RIGHT))->
        render();

    // Reset every mount — the flag persisted across dismount, so re-entering
    // Sleep after a prior cycle would fire the transition on the first loop
    // tick before the new dormantTimer ever ran.
    transitionToAwakenSequenceState = false;
    dormantTimer.setTimer(SLEEP_DURATION);
}

void Sleep::onStateLoop(PDN* pdn) {

    dormantTimer.updateTime();

    if(dormantTimer.expired()) {
        transitionToAwakenSequenceState = true;
    }
}

void Sleep::onStateDismounted(PDN* pdn) {
    dormantTimer.invalidate();
}

bool Sleep::transitionToAwakenSequence() {
    return transitionToAwakenSequenceState;
}