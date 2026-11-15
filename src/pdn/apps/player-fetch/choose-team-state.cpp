#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "device/firmware-update-manager.hpp"
#include "id-generator.hpp"

ChooseTeamState::ChooseTeamState(PlayerProvider* playerProvider,
                                 FirmwareUpdateManager* firmwareUpdateManager, bool testIdentity)
    : TypedState<PDN>(PlayerFetchStateId::CHOOSE_TEAM)
    , playerProvider(playerProvider)
    , firmwareUpdateManager(firmwareUpdateManager)
    , testIdentity(testIdentity) {}

ChooseTeamState::~ChooseTeamState() {
    playerProvider = nullptr;
    firmwareUpdateManager = nullptr;
}

void ChooseTeamState::onStateMounted(PDN* pdn) {
    playerProvider->clear();
    macAddress = pdn->getWirelessManager()->getMacAddress();
    sinceMount.setTimer(0);
    // The first screen after boot is where a PDN takes an ESP-NOW firmware update.
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(true);
    }
    renderUi(pdn);

    pdn->getPrimaryButton()->setButtonPress(
    [](void* ctx) {
        ChooseTeamState* chooseTeamState = static_cast<ChooseTeamState*>(ctx);
        chooseTeamState->hunterSelected = !chooseTeamState->hunterSelected;
        chooseTeamState->displayIsDirty = true;
    },
    this, ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress(
    [](void* ctx) {
        ChooseTeamState* chooseTeamState = static_cast<ChooseTeamState*>(ctx);
        Player* player = chooseTeamState->playerProvider->getPlayer();
        Team team;
        IdGenerator::copyId(team.name, chooseTeamState->hunterSelected ? "Hunter" : "Bounty");
        player->applyRegistrationProfile(chooseTeamState->testIdentity
                                         ? Player::testPlayerIdFromMac(chooseTeamState->macAddress)
                                         : UUIDBytes(),
                                         chooseTeamState->testIdentity ? "Test" : "Demo", team, {});
        chooseTeamState->transitionToHubState = true;
    },
    this, ButtonInteraction::CLICK);
}

void ChooseTeamState::onStateLoop(PDN* pdn) {
    // The pressed time counts from press-down, so a hold carried in from the
    // previous screen is longer than this one has been up and does not count.
    const unsigned long heldMs = pdn->getSecondaryButton()->longPressedMillis();
    if (heldMs >= PLAYER_FETCH_FIRMWARE_HOLD_MS && heldMs <= sinceMount.getElapsedTime()) {
        transitionToFirmwareUpdateState = true;
    }
    if (displayIsDirty) {
        renderUi(pdn);
        displayIsDirty = false;
    }
}

void ChooseTeamState::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    transitionToHubState = false;
    transitionToFirmwareUpdateState = false;
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(false);
    }
    macAddress = nullptr;
    displayIsDirty = false;
    hunterSelected = true;
}

bool ChooseTeamState::transitionToHub() const { return transitionToHubState; }

bool ChooseTeamState::transitionToFirmwareUpdate() const {
    return transitionToFirmwareUpdateState;
}

void ChooseTeamState::renderUi(PDN* pdn) const {
    pdn->getDisplay()->invalidateScreen();

    pdn->getDisplay()->setGlyphMode(FontMode::TEXT)->drawText("Choose Role", 3, 16);

    if (hunterSelected) {
        pdn->getDisplay()->drawButton("HUNTER", 64, 36)->drawText("BOUNTY", 25, 56);
    } else {
        pdn->getDisplay()->drawText("HUNTER", 25, 36)->drawButton("BOUNTY", 64, 56);
    }

    pdn->getDisplay()->render();
}
