#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"

PlayerFetchWelcome::PlayerFetchWelcome(Player* player, PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::WELCOME)
    , player(player)
    , playerProvider(playerProvider) {}

PlayerFetchWelcome::~PlayerFetchWelcome() {
    player = nullptr;
    playerProvider = nullptr;
}

void PlayerFetchWelcome::onStateMounted(PDN* pdn) {
    const std::string fallbackName = player->getName();
    const char* name = fallbackName.c_str();
    if (playerProvider->hasPlayer() && playerProvider->getPlayer().name[0] != '\0') {
        name = playerProvider->getPlayer().name;
    }

    pdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText("Welcome", 0, 24)
    ->drawText(name, 0, 44)
    ->render();

    welcomeMessageTimer.setTimer(WELCOME_MESSAGE_TIMEOUT_MS);
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
}

void PlayerFetchWelcome::onStateLoop(PDN* pdn) {
    welcomeMessageTimer.updateTime();
    if (welcomeMessageTimer.expired()) {
        transitionToAwakenState = true;
    }
}

void PlayerFetchWelcome::onStateDismounted(PDN* pdn) {
    welcomeMessageTimer.invalidate();
    transitionToAwakenState = false;
}

bool PlayerFetchWelcome::transitionToAwaken() const { return transitionToAwakenState; }
