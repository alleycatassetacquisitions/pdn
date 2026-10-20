#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "id-generator.hpp"

ChooseTeamState::ChooseTeamState(Player* player, PlayerFetchSession* session)
    : TypedState<PDN>(PlayerFetchStateId::CHOOSE_TEAM)
    , player(player)
    , session(session) {}

ChooseTeamState::~ChooseTeamState() {
    player = nullptr;
    session = nullptr;
}

void ChooseTeamState::onStateMounted(PDN* pdn) {
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
        DevicePlayer demoProfile;
        IdGenerator::copyId(demoProfile.name, "Demo");
        IdGenerator::copyId(demoProfile.teamName,
                            chooseTeamState->hunterSelected ? "Hunter" : "Bounty");
        chooseTeamState->session->setDevicePlayer(demoProfile);
        chooseTeamState->player->setName("Demo");
        chooseTeamState->transitionToDemoStubState = true;
    },
    this, ButtonInteraction::CLICK);
}

void ChooseTeamState::onStateLoop(PDN* pdn) {
    if (displayIsDirty) {
        renderUi(pdn);
        displayIsDirty = false;
    }
}

void ChooseTeamState::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    transitionToDemoStubState = false;
    displayIsDirty = false;
    hunterSelected = true;
}

bool ChooseTeamState::transitionToDemoStub() const { return transitionToDemoStubState; }

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
