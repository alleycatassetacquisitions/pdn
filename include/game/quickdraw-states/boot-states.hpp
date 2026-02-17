#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/http-client-interface.hpp"
#include <string>

// Forward declarations
class ProgressManager;

/*
 * PlayerRegistration - Initial boot state for entering 4-digit user ID
 */
class PlayerRegistration : public State {
public:
    PlayerRegistration(Player* player, MatchManager* matchManager);
    ~PlayerRegistration();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
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

/*
 * FetchUserDataState - Fetches user profile from server
 */
class FetchUserDataState : public State {
public:
    FetchUserDataState(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager, ProgressManager* progressManager);
    ~FetchUserDataState();

    bool transitionToConfirmOffline();
    bool transitionToWelcomeMessage();
    bool transitionToUploadMatches();
    bool transitionToPlayerRegistration();
    void showLoadingGlyphs(Device *PDN);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

private:
    RemoteDebugManager* remoteDebugManager;
    ProgressManager* progressManager;
    bool transitionToPlayerRegistrationState = false;
    bool transitionToConfirmOfflineState = false;
    bool transitionToWelcomeMessageState = false;
    bool transitionToUploadMatchesState = false;
    WirelessManager* wirelessManager;
    bool isFetchingUserData = false;
    Player* player;
    SimpleTimer userDataFetchTimer;
    const int USER_DATA_FETCH_TIMEOUT = 10000;
};

/*
 * ConfirmOfflineState - Prompts user to confirm offline mode or re-enter ID
 */
class ConfirmOfflineState : public State {
public:
    explicit ConfirmOfflineState(Player* player);
    ~ConfirmOfflineState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToChooseRole();
    bool transitionToPlayerRegistration();
    void renderUi(Device *PDN);
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
