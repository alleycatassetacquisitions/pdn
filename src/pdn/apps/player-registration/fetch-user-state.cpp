#include "apps/player-registration/player-registration-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/remote-debug-manager.hpp"

static const char* TAG = "FetchUserDataState";

FetchUserDataState::FetchUserDataState(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager)
    : TypedState<PDN>(PlayerRegistrationStateId::FETCH_USER_DATA) {
    LOG_I(TAG, "Initializing FetchUserDataState");
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->remoteDebugManager = remoteDebugManager;
}

FetchUserDataState::~FetchUserDataState() {
    LOG_I(TAG, "Destroying FetchUserDataState");
    remoteDebugManager = nullptr;
    wirelessManager = nullptr;
    player = nullptr;
}   

void FetchUserDataState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - Starting user data fetch");
    renderLoadingScreen(pdn->getDisplay());

    LOG_I(TAG, "Player ID for fetch: %s", player->getUserID().c_str());

    if(player->getUserID() == TEST_BOUNTY_ID) {
        player->setIsHunter(false);
        player->setName("KO-NA-MI");
        player->setFaction("Bounty");
        transitionToWelcomeMessageState = true;
    } else if(player->getUserID() == TEST_HUNTER_ID) {
        player->setIsHunter(true);
        player->setName("Nesting Bot");
        player->setFaction("Hunter");
        transitionToWelcomeMessageState = true;
    } else if(player->getUserID() == BROADCAST_WIFI) { 
        remoteDebugManager->BroadcastDebugPacket();
        transitionToPlayerRegistrationState = true;
    } else {
        fetchUserData();
    }
}   

void FetchUserDataState::onStateLoop(PDN* pdn) {
    fetchTimer.updateTime();

    if(fetchTimer.expired()) {
        LOG_W(TAG, "User data fetch timer expired");
        transitionToConfirmOfflineState = true;
    } else if(fetchTimer.isRunning()) {
        if(SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            renderLoadingScreen(pdn->getDisplay());
        }
    }
}   

void FetchUserDataState::onStateDismounted(PDN* pdn) {
    LOG_I(TAG, "State dismounted");
    if (wirelessManager) {
        wirelessManager->enablePeerCommsMode();
    }
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    transitionToConfirmOfflineState = false;
    transitionToWelcomeMessageState = false;
    transitionToPlayerRegistrationState = false;
    fetchTimer.invalidate();
}

void FetchUserDataState::fetchUserData() {
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

            fetchTimer.invalidate();
            transitionToWelcomeMessageState = true;
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to fetch player data: %s (code: %d), willRetry: %d", 
                error.message.c_str(), static_cast<int>(error.code), error.willRetry);
            if (!error.willRetry) {
                player->setName("Unknown");
                player->setAllegiance("None");
                transitionToConfirmOfflineState = true;
            }
        }
    );
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