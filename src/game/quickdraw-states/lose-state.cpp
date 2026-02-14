#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"

Lose::Lose(Player *player) : State(LOSE) {
    this->player = player;
}

Lose::~Lose() {
    player = nullptr;
}

void Lose::onStateMounted(Device *PDN) {
    PDN->getDisplay()->invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::LOSE))->
    render();

    loseTimer.setTimer(8000);

    AnimationConfig config;
    config.type = AnimationType::LOSE;
    config.loop = true;
    config.speed = 16;
    config.initialState = LEDState();
    config.loopDelayMs = 0;

    PDN->getLightManager()->startAnimation(config);
}

void Lose::onStateLoop(Device *PDN) {
    loseTimer.updateTime();
    if(loseTimer.expired()) {
        reset = true;
    }
}

void Lose::onStateDismounted(Device *PDN) {
    loseTimer.invalidate();
    reset = false;
}

bool Lose::resetGame() {
    return reset;
}

bool Lose::isTerminalState() const {
    return true;
}
