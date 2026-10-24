#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"

PlayerFetchWelcome::PlayerFetchWelcome(PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::WELCOME)
    , playerProvider(playerProvider) {}

PlayerFetchWelcome::~PlayerFetchWelcome() { playerProvider = nullptr; }

void PlayerFetchWelcome::onStateMounted(PDN* pdn) {
    const char* name = "";
    Player* player = playerProvider->getPlayer();
    if (playerProvider->hasPlayer() && player->getServerPlayerName()[0] != '\0') {
        name = player->getServerPlayerName();
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
