#pragma once

#include "device/pdn.hpp"
#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

namespace PlayerFetchStateId {
constexpr int CHOOSE_TEAM = 2;
constexpr int FIRMWARE_UPDATE = 31;
}  // namespace PlayerFetchStateId

constexpr unsigned long PLAYER_FETCH_FIRMWARE_HOLD_MS = 5000;

class ChooseTeamState : public TypedState<PDN> {
public:
    /** Role picker: MAC test UUID and "Test" if @p testIdentity, else nil UUID and "Demo". */
    ChooseTeamState(PlayerProvider* playerProvider, bool testIdentity);
    /** @copydoc TypedState::~TypedState */
    ~ChooseTeamState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true once a role is confirmed and the hub should launch. */
    bool transitionToHub() const;
    /** @return true when the secondary button was held for the firmware-update hold. */
    bool transitionToFirmwareUpdate() const;

private:
    void renderUi(PDN* pdn) const;

    PlayerProvider* playerProvider = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    const uint8_t* macAddress = nullptr;
    SimpleTimer sinceMount;
    bool testIdentity = false;
    bool transitionToHubState = false;
    bool transitionToFirmwareUpdateState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};
