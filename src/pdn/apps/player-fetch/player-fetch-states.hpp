#pragma once

#include "device/pdn.hpp"
#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

namespace PlayerFetchStateId {
constexpr int PIN_ENTRY = 0;
constexpr int CHOOSE_TEAM = 2;
constexpr int FIRMWARE_UPDATE = 31;
}  // namespace PlayerFetchStateId

constexpr unsigned long PLAYER_FETCH_FIRMWARE_HOLD_MS = 5000;

class PinEntryState : public TypedState<PDN> {
public:
    /** Pairing-code entry; while it is up, @p firmwareUpdateManager (may be null) takes updates. */
    PinEntryState(PlayerProvider* playerProvider, FirmwareUpdateManager* firmwareUpdateManager);
    /** @copydoc TypedState::~TypedState */
    ~PinEntryState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true once four digits are entered; every PIN leads to the role picker. */
    bool transitionToChooseTeam() const;
    /** @return true when secondary-button OTA hold completed on pin entry. */
    bool transitionToFirmwareUpdate() const;

private:
    bool transitionToChooseTeamState = false;
    bool transitionToFirmwareUpdateState = false;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    SimpleTimer sinceMount;
    bool shouldRender = false;
    PlayerProvider* playerProvider = nullptr;
    int currentDigit = 0;
    int currentDigitIndex = 0;
    static constexpr int DIGIT_COUNT = 4;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
};

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

private:
    void renderUi(PDN* pdn) const;

    PlayerProvider* playerProvider = nullptr;
    const uint8_t* macAddress = nullptr;
    bool testIdentity = false;
    bool transitionToHubState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};
