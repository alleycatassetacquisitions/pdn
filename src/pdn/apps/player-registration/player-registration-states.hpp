#pragma once

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
    FetchUserDataState(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager, MatchManager* matchManager);
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
    RemoteDebugManager* remoteDebugManager;
    MatchManager* matchManager;
    bool transitionToPlayerRegistrationState = false;
    bool transitionToConfirmOfflineState = false;
    bool transitionToWelcomeMessageState = false;
    WirelessManager* wirelessManager;
    bool isFetchingUserData = false;
    bool isUploadingMatches = false;

    Player* player;
    SimpleTimer fetchTimer;
    const int USER_DATA_FETCH_TIMEOUT = 10000;
    const int MATCHES_UPLOAD_TIMEOUT = 10000;
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