#pragma once

#include "device/crash/crash-record-source.hpp"
#include "device/pdn.hpp"
#include "state/state.hpp"
#include "game/player.hpp"
#include "game/match-manager.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "utils/simple-timer.hpp"
#include "game/quickdraw-resources.hpp"

enum PlayerRegistrationStateId {
    PLAYER_REGISTRATION = 0,
    FETCH_USER_DATA = 1,
    CONFIRM_OFFLINE = 2,
    CHOOSE_ROLE = 3,
    ALLEGIANCE_PICKER = 4,
    WELCOME_MESSAGE = 5,
};

class PlayerRegistrationState : public TypedState<PDN> {
public:
    PlayerRegistrationState(Player* player, MatchManager* matchManager);
    ~PlayerRegistrationState();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToUserFetch();

private:
    bool transitionToUserFetchState = false;
    bool shouldRender = false;
    Player* player;
    MatchManager* matchManager;
    int currentDigit = 0;
    int currentDigitIndex = 0;
    static constexpr int DIGIT_COUNT = 4;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
};

class FetchUserDataState : public TypedState<PDN> {
public:
    /**
     * After a successful player fetch, uploads pending records from
     * `crashRecordSource` oldest first, until one fails or CRASH_UPLOAD_TIMEOUT
     * runs out; nullptr skips that step.
     */
    FetchUserDataState(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager, MatchManager* matchManager, CrashRecordSource* crashRecordSource);
    ~FetchUserDataState();

    bool transitionToConfirmOffline();
    bool transitionToWelcomeMessage();
    bool transitionToPlayerRegistration();
    void fetchUserData();
    void uploadMatches();
    void showLoadingGlyphs(PDN* pdn);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    
private:
    void uploadNextCrashRecord();
    void finishCrashUpload();
    uint32_t beginRequest();
    bool isCurrentRequest(uint32_t requestId) const;

    RemoteDebugManager* remoteDebugManager;
    MatchManager* matchManager;
    CrashRecordSource* crashRecordSource;
    bool transitionToPlayerRegistrationState = false;
    bool transitionToConfirmOfflineState = false;
    bool transitionToWelcomeMessageState = false;
    WirelessManager* wirelessManager;
    // Which exchange the shared fetchTimer is timing, so expiry knows where to go.
    enum class FetchPhase { IDLE,
                            UPLOADING_MATCHES,
                            FETCHING_PLAYER,
                            UPLOADING_CRASH_RECORDS };
    FetchPhase phase = FetchPhase::IDLE;
    // Every request captures the id it was issued under; dismount, each new
    // request and the end of the crash upload move it on, so a late answer to an
    // earlier one does not advance the flow (it still clears uploaded matches
    // or marks an uploaded crash record).
    uint32_t currentRequestId = 0;

    Player* player;
    SimpleTimer fetchTimer;
    const int USER_DATA_FETCH_TIMEOUT = 10000;
    const int MATCHES_UPLOAD_TIMEOUT = 10000;
    // For all pending crash records together; whatever is left goes on a later boot.
    static constexpr int CRASH_UPLOAD_TIMEOUT = 15000;
};

class ConfirmOfflineState : public TypedState<PDN> {
public:
    explicit ConfirmOfflineState(Player* player);
    ~ConfirmOfflineState();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToChooseRole();
    bool transitionToPlayerRegistration();
    void renderUi(PDN* pdn);
    int getDigitGlyphForIDIndex(int index);

private:
    Player* player;
    int uiPage = 0;
    const int UI_PAGE_COUNT = 3;
    const int UI_PAGE_TIMEOUT = 3000;
    bool finishedPaging = false;
    int menuIndex = 0;
    bool displayIsDirty = false;
    const int MENU_ITEM_COUNT = 2;
    SimpleTimer uiPageTimer;
    bool transitionToChooseRoleState = false;
    bool transitionToPlayerRegistrationState = false;
    parameterizedCallbackFunction confirmCallback;
    parameterizedCallbackFunction cancelCallback;
};

class ChooseRoleState : public TypedState<PDN> {
public:
    explicit ChooseRoleState(Player* player);
    ~ChooseRoleState();
    
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToWelcomeMessage();
    void renderUi(PDN* pdn);

private:
    Player* player;
    bool transitionToWelcomeMessageState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};

class WelcomeMessage : public TypedState<PDN> {
public:
    explicit WelcomeMessage(Player* player);
    ~WelcomeMessage();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    void renderWelcomeMessage(PDN* pdn);
    bool transitionToGameplay();

private:
    Player* player;
    SimpleTimer welcomeMessageTimer;
    bool transitionToAwakenSequenceState = false;
    const int WELCOME_MESSAGE_TIMEOUT = 5000;
};