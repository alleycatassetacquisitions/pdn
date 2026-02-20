#pragma once

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

class PlayerRegistrationState : public State {
public:
    PlayerRegistrationState(Player* player, MatchManager* matchManager);
    ~PlayerRegistrationState();

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

class FetchUserDataState : public State {
public:
    FetchUserDataState(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager, MatchManager* matchManager);
    ~FetchUserDataState();

    bool transitionToConfirmOffline();
    bool transitionToWelcomeMessage();
    bool transitionToPlayerRegistration();
    void fetchUserData();
    void uploadMatches();
    void showLoadingGlyphs(Device *PDN);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    
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


class ChooseRoleState : public State {
public:
    explicit ChooseRoleState(Player* player);
    ~ChooseRoleState();
    
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToWelcomeMessage();
    void renderUi(Device *PDN);

private:
    Player* player;
    bool transitionToWelcomeMessageState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};

class WelcomeMessage : public State {
public:
    explicit WelcomeMessage(Player* player);
    ~WelcomeMessage();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void renderWelcomeMessage(Device *PDN);
    bool transitionToGameplay();

private:
    Player* player;
    SimpleTimer welcomeMessageTimer;
    bool transitionToAwakenSequenceState = false;
    const int WELCOME_MESSAGE_TIMEOUT = 5000;
};