//
// Created by Elli Furedy on 9/30/2024.
//
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include <string>
#include "game/quickdraw-requests.hpp"
#include "device/device.hpp"

#define TAG "SleepState"

using namespace std;

Sleep::Sleep(Player* player) : TypedState<PDN>(SLEEP) {
    this->player = player;
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

    // TODO: reimplement this breathing effect as an Animation (no direct-LED
    // setLight API exists).
    /*
    EVERY_N_MILLIS(16) {
        if (breatheUp) {
            ledBrightness++;
        } else {
            ledBrightness--;
        }
        pwm_val =
            255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

        if (ledBrightness == 255) {
            breatheUp = false;
        } else if (ledBrightness == 0) {
            breatheUp = true;
        }

        LEDColor color = bountyColors[random8()];
        pdn->getLightManager()->setLight(LightIdentifier::TRANSMIT_LIGHT, 0, color);
    }
    */
}

void Sleep::onStateDismounted(PDN* pdn) {
    dormantTimer.invalidate();
}

bool Sleep::transitionToAwakenSequence() {
    return transitionToAwakenSequenceState;
}