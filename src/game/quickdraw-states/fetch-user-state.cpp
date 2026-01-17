#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "logger.hpp"
#include "wireless/remote-debug-manager.hpp"

static const char* TAG = "FetchUserDataState";

FetchUserDataState::FetchUserDataState(Player* player, HttpClientInterface* httpClient) : State(QuickdrawStateId::FETCH_USER_DATA) {
    LOG_I(TAG, "Initializing FetchUserDataState");
    this->player = player;
    this->httpClient = httpClient;
}   

FetchUserDataState::~FetchUserDataState() {
    LOG_I(TAG, "Destroying FetchUserDataState");
}   

void FetchUserDataState::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted - Starting user data fetch");
    showLoadingGlyphs(PDN);
    isFetchingUserData = true;
    userDataFetchTimer.setTimer(20000);
    
    LOG_I(TAG, "Player ID for fetch: %s", player->getUserID().c_str());

    if(player->getUserID() == TEST_BOUNTY_ID) {
        player->setIsHunter(false);
        player->setName("KO-NA-MI");
        player->setFaction("Bounty");
        transitionToWelcomeMessageState = true;
        userDataFetchTimer.invalidate();
        isFetchingUserData = false;
    } else if(player->getUserID() == TEST_HUNTER_ID) {
        player->setIsHunter(true);
        player->setName("Nesting Bot");
        player->setFaction("Hunter");
        transitionToWelcomeMessageState = true;
        userDataFetchTimer.invalidate();
        isFetchingUserData = false;
    } else if(player->getUserID() == FORCE_MATCH_UPLOAD) {
        transitionToUploadMatchesState = true;
        userDataFetchTimer.invalidate();
        isFetchingUserData = false;
    } else if(player->getUserID() == BROADCAST_WIFI) { 
        RemoteDebugManager::GetInstance()->BroadcastDebugPacket();
        transitionToPlayerRegistrationState = true;
        userDataFetchTimer.invalidate();
        isFetchingUserData = false;
    } else {
        QuickdrawRequests::getPlayer(
            httpClient,
            player->getUserID(),
            [this](const PlayerResponse& response) {
                LOG_I(TAG, "Successfully fetched player data: %s (%s)", 
                        response.name.c_str(), response.id.c_str());
                
                player->setName(response.name.c_str());
                player->setIsHunter(response.isHunter);
                player->setAllegiance(response.allegiance);
                player->setFaction(response.faction.c_str());

                userDataFetchTimer.invalidate();
                transitionToWelcomeMessageState = true;
            },
            [this](const WirelessErrorInfo& error) {
                LOG_E(TAG, "Failed to fetch player data: %s (code: %d), willRetry: %d", 
                    error.message.c_str(), static_cast<int>(error.code), error.willRetry);
                if(!error.willRetry) {
                    isFetchingUserData = false;
                    player->setName("Unknown");
                    player->setAllegiance("None");
                    transitionToConfirmOfflineState = true;
                }
            }
        );
    }
}   

void FetchUserDataState::onStateLoop(Device *PDN) {
    userDataFetchTimer.updateTime();

    if(userDataFetchTimer.expired()) {
        LOG_W(TAG, "User data fetch timer expired");
        isFetchingUserData = false;
        transitionToConfirmOfflineState = true;
    } else if(userDataFetchTimer.isRunning()) {
        if(SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            showLoadingGlyphs(PDN);
        }
    }
}   

void FetchUserDataState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    isFetchingUserData = false;
    transitionToConfirmOfflineState = false;
    transitionToWelcomeMessageState = false;
    transitionToUploadMatchesState = false;
    transitionToPlayerRegistrationState = false;
    userDataFetchTimer.invalidate();
}   

bool FetchUserDataState::transitionToConfirmOffline() {
    return transitionToConfirmOfflineState;
}  

bool FetchUserDataState::transitionToUploadMatches() {
    return transitionToUploadMatchesState;
}

bool FetchUserDataState::transitionToPlayerRegistration() {
    return transitionToPlayerRegistrationState;
}

void FetchUserDataState::showLoadingGlyphs(Device *PDN) {
    const int GLYPH_SIZE = 14;
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;
    
    const int GLYPHS_PER_ROW = (SCREEN_WIDTH / GLYPH_SIZE);
    const int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE / GLYPH_SIZE);
    
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::LOADING_GLYPH);
    
    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if(rand() % 100 < 50) {
                int x = col * GLYPH_SIZE;
                int y = 14 + (row * GLYPH_SIZE);
                int randomIndex = rand() % 8;
                const char* glyph = loadingGlyphs[randomIndex];
                PDN->getDisplay()->renderGlyph(glyph, x, y);
            }
        }
    }
    
    PDN->getDisplay()->render();
}  

bool FetchUserDataState::transitionToWelcomeMessage() {
    return transitionToWelcomeMessageState;
}
