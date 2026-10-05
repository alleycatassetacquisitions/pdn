#include "apps/player-registration/player-registration-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/remote-debug-manager.hpp"

static const char* TAG = "FetchUserDataState";

FetchUserDataState::FetchUserDataState(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager, MatchManager* matchManager) : TypedState<PDN>(PlayerRegistrationStateId::FETCH_USER_DATA) {
    LOG_I(TAG, "Initializing FetchUserDataState");
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->remoteDebugManager = remoteDebugManager;
    this->matchManager = matchManager;
}   

FetchUserDataState::~FetchUserDataState() {
    LOG_I(TAG, "Destroying FetchUserDataState");
    remoteDebugManager = nullptr;
    wirelessManager = nullptr;
    player = nullptr;
}   

void FetchUserDataState::clearActiveRequest() {
    fetchTimer.invalidate();
    activeRequest = ActiveRequest::NONE;
}

void FetchUserDataState::onActiveRequestTimedOut() {
    switch (activeRequest) {
        case ActiveRequest::USER_DATA_FETCH:
            LOG_W(TAG, "User data fetch timer expired");
            transitionToConfirmOfflineState = true;
            break;
        case ActiveRequest::HEALTH_CHECK:
            LOG_W(TAG, "Health check timer expired");
            transitionToPlayerRegistrationState = true;
            break;
        case ActiveRequest::MATCHES_UPLOAD:
            LOG_W(TAG, "Matches upload timer expired");
            transitionToPlayerRegistrationState = true;
            break;
        case ActiveRequest::NONE:
            LOG_E(TAG, "No active request, but timer expired");
            transitionToPlayerRegistrationState = true;
            break;
    }
    clearActiveRequest();
}

void FetchUserDataState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - Starting user data fetch");
    showLoadingGlyphs(pdn);
    
    LOG_I(TAG, "Player ID for fetch: %s", player->getUserID().c_str());

    if (player->getUserID() == TEST_BOUNTY_ID) {
        player->setIsHunter(false);
        player->setName("KO-NA-MI");
        player->setFaction("Bounty");
        transitionToWelcomeMessageState = true;
        clearActiveRequest();
    } else if (player->getUserID() == TEST_HUNTER_ID) {
        player->setIsHunter(true);
        player->setName("Nesting Bot");
        player->setFaction("Hunter");
        transitionToWelcomeMessageState = true;
        clearActiveRequest();
    } else if (player->getUserID() == BROADCAST_WIFI) { 
        remoteDebugManager->BroadcastDebugPacket();
        transitionToPlayerRegistrationState = true;
        clearActiveRequest();
    } else if (player->getUserID() == HEALTH_CHECK) {
        healthCheck();
    } else if (matchManager->getStoredMatchCount() > 0) {
        uploadMatches();
    } else {
        fetchUserData();
    }
}   

void FetchUserDataState::onStateLoop(PDN* pdn) {
    fetchTimer.updateTime();

    if (fetchTimer.expired()) {
        onActiveRequestTimedOut();
    } else if (fetchTimer.isRunning()) {
        if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            showLoadingGlyphs(pdn);
        }
    }
}   

void FetchUserDataState::onStateDismounted(PDN* pdn) {
    LOG_I(TAG, "State dismounted");
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    clearActiveRequest();
    transitionToConfirmOfflineState = false;
    transitionToWelcomeMessageState = false;
    transitionToPlayerRegistrationState = false;
}   

void FetchUserDataState::uploadMatches() {
    activeRequest = ActiveRequest::MATCHES_UPLOAD;
    fetchTimer.setTimer(MATCHES_UPLOAD_TIMEOUT);
    QuickdrawRequests::updateMatches(
        wirelessManager,
        matchManager->toJson(),
        [this](const std::string& jsonResponse) {
            LOG_I(TAG, "Successfully uploaded matches: %s", jsonResponse.c_str());
            matchManager->clearStorage();
            clearActiveRequest();
            fetchUserData();
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to upload matches: %s (code: %d)", 
                error.message.c_str(), static_cast<int>(error.code));
            clearActiveRequest();
            fetchUserData();
        }
    );
}

void FetchUserDataState::fetchUserData() {  
    activeRequest = ActiveRequest::USER_DATA_FETCH;
    fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT);
    QuickdrawRequests::getPlayer(
        wirelessManager,
        player->getUserID(),
        [this](const PlayerResponse& response) {
            LOG_I(TAG, "Successfully fetched player data: %s (%s)", 
                    response.name.c_str(), response.id.c_str());
            
            player->setName(response.name.c_str());
            player->setIsHunter(response.isHunter);
            player->setAllegiance(response.allegiance);
            player->setFaction(response.faction.c_str());

            clearActiveRequest();
            transitionToWelcomeMessageState = true;
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to fetch player data: %s (code: %d), willRetry: %d", 
                error.message.c_str(), static_cast<int>(error.code), error.willRetry);
            if (!error.willRetry) {
                player->setName("Unknown");
                player->setAllegiance("None");
                clearActiveRequest();
                transitionToConfirmOfflineState = true;
            }
        }
    );
}

void FetchUserDataState::healthCheck() {
    activeRequest = ActiveRequest::HEALTH_CHECK;
    fetchTimer.setTimer(HEALTH_CHECK_TIMEOUT);
    QuickdrawRequests::healthCheck(
        wirelessManager,
        [this](const std::string& success) {
            LOG_I(TAG, "HEALTH CHECK SUCCESS");
            clearActiveRequest();
            transitionToPlayerRegistrationState = true;
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "HEALTH CHECK ERROR: %s", error.message.c_str());
            clearActiveRequest();
            transitionToPlayerRegistrationState = true;
        }
    );
}

void FetchUserDataState::showLoadingGlyphs(PDN* pdn) {
    renderLoadingScreen(pdn->getDisplay());
}  

bool FetchUserDataState::transitionToWelcomeMessage() {
    return transitionToWelcomeMessageState;
}

bool FetchUserDataState::transitionToPlayerRegistration() {
    return transitionToPlayerRegistrationState;
}

bool FetchUserDataState::transitionToConfirmOffline() {
    return transitionToConfirmOfflineState;
}
