#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"

Win::Win(Player *player) : State(WIN) {
    this->player = player;
}

Win::~Win() {
    player = nullptr;
}

void Win::onStateMounted(Device *PDN) {
    PDN->setVibration(VIBRATION_OFF);

    PDN->invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::WIN))->
    render();

    winTimer.setTimer(8000);

    AnimationConfig config;
    config.type = player->isHunter() ? AnimationType::HUNTER_WIN : AnimationType::BOUNTY_WIN;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;

    PDN->startAnimation(config);
}

void Win::onStateLoop(Device *PDN) {
    winTimer.updateTime();
    if(winTimer.expired()) {
        reset = true;
    }
}

void Win::onStateDismounted(Device *PDN) {
    winTimer.invalidate();
    reset = false;
    PDN->setVibration(VIBRATION_OFF);
}

bool Win::resetGame() {
    return reset;
}

bool Win::isTerminalState() {
    return true;
}
