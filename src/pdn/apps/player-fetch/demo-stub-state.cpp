#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"

DemoStubState::DemoStubState(Player* player)
    : TypedState<PDN>(PlayerFetchStateId::DEMO_STUB)
    , player(player) {}

DemoStubState::~DemoStubState() { player = nullptr; }

void DemoStubState::onStateMounted(PDN* pdn) {
    pdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText("Demo", 40, 32)
    ->render();

    pdn->getSecondaryButton()->setButtonPress(
    [](void* ctx) {
        DemoStubState* demoStub = static_cast<DemoStubState*>(ctx);
        demoStub->transitionToPinEntryState = true;
    },
    this, ButtonInteraction::CLICK);
}

void DemoStubState::onStateLoop(PDN* pdn) {}

void DemoStubState::onStateDismounted(PDN* pdn) {
    pdn->getSecondaryButton()->removeButtonCallbacks();
    transitionToPinEntryState = false;
}

bool DemoStubState::transitionToPinEntry() const { return transitionToPinEntryState; }
