#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include <string>

// Forward declarations
class FdnResultManager;

/*
 * UploadMatchesState - Uploads match results to server
 */
class UploadMatchesState : public State {
public:
    UploadMatchesState(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager, FdnResultManager* fdnResultManager);
    ~UploadMatchesState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToSleep();
    void showLoadingGlyphs(Device *PDN);
    bool transitionToPlayerRegistration();
    void routeToNextState();
    void attemptUpload();
    void attemptFdnUpload();

private:
    Player* player;
    WirelessManager* wirelessManager;
    MatchManager* matchManager;
    FdnResultManager* fdnResultManager;
    SimpleTimer uploadMatchesTimer;
    int matchUploadRetryCount = 0;
    int fdnUploadRetryCount = 0;
    const int UPLOAD_MATCHES_TIMEOUT = 10000;
    std::string matchesJson;
    bool transitionToSleepState = false;
    bool transitionToPlayerRegistrationState = false;
    bool shouldRetryUpload = false;
    bool shouldRetryFdnUpload = false;
    bool matchesUploaded = false;
    bool fdnResultsUploaded = false;
};
