#include "game/quickdraw-states.hpp"
#include "player.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw.hpp"
#include <esp_log.h>

static const char* TAG = "WelcomeMessage";

WelcomeMessage::WelcomeMessage(Player* player) : State(QuickdrawStateId::WELCOME_MESSAGE) {
    this->player = player;
}

WelcomeMessage::~WelcomeMessage() {
}

void WelcomeMessage::onStateMounted(Device *PDN) {
    ESP_LOGI(TAG, "WelcomeMessage state mounted");
    renderWelcomeMessage();
    welcomeMessageTimer.setTimer(WELCOME_MESSAGE_TIMEOUT);
    PDN->setActiveComms(player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK);
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);

} 

void WelcomeMessage::onStateLoop(Device *PDN) {
    welcomeMessageTimer.updateTime();
    if(welcomeMessageTimer.expired()) {
        transitionToAwakenSequenceState = true;
    }
}

void WelcomeMessage::renderWelcomeMessage() {
    PDN::GetInstance()->
    invalidateScreen()->
    setGlyphMode(FontMode::TEXT)->
    drawText("**Alias**", 0, 16)->
    drawText(player->getName().c_str(), 0, 32)->
    drawText("*Allegiance*", 0, 48)->
    drawText(player->getAllegianceString().c_str(), 0, 64)->
    render();
}

void WelcomeMessage::onStateDismounted(Device *PDN) {
    ESP_LOGI(TAG, "WelcomeMessage state dismounted");
    welcomeMessageTimer.invalidate();
    transitionToAwakenSequenceState = false;
}

bool WelcomeMessage::transitionToGameplay() {
    return transitionToAwakenSequenceState;
}

