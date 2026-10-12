#include "apps/player-registration/player-registration-states.hpp"
#include "alleycat-server/crash-encoder.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/remote-debug-manager.hpp"

static const char* TAG = "FetchUserDataState";

FetchUserDataState::FetchUserDataState(Player* player, WirelessManager* wirelessManager, RemoteDebugManager* remoteDebugManager, MatchManager* matchManager, CrashRecordSource* crashRecordSource)
    : TypedState<PDN>(PlayerRegistrationStateId::FETCH_USER_DATA)
    , crashRecordSource(crashRecordSource) {
    LOG_I(TAG, "Initializing FetchUserDataState");
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->remoteDebugManager = remoteDebugManager;
    this->matchManager = matchManager;
}

FetchUserDataState::~FetchUserDataState() {
    LOG_I(TAG, "Destroying FetchUserDataState");
    remoteDebugManager = nullptr;
    crashRecordSource = nullptr;
    wirelessManager = nullptr;
    player = nullptr;
}   

void FetchUserDataState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - Starting user data fetch");
    showLoadingGlyphs(pdn);
    
    LOG_I(TAG, "Player ID for fetch: %s", player->getUserID().c_str());

    if(player->getUserID() == TEST_BOUNTY_ID) {
        player->setIsHunter(false);
        player->setName("KO-NA-MI");
        player->setFaction("Bounty");
        transitionToWelcomeMessageState = true;
        fetchTimer.invalidate();
    } else if(player->getUserID() == TEST_HUNTER_ID) {
        player->setIsHunter(true);
        player->setName("Nesting Bot");
        player->setFaction("Hunter");
        transitionToWelcomeMessageState = true;
        fetchTimer.invalidate();
    } else if(player->getUserID() == BROADCAST_WIFI) { 
        remoteDebugManager->BroadcastDebugPacket();
        transitionToPlayerRegistrationState = true;
        fetchTimer.invalidate();
    } else if(matchManager->getStoredMatchCount() > 0) {
        uploadMatches();
    } else {
        fetchUserData();
    }
}   

void FetchUserDataState::onStateLoop(PDN* pdn) {
    fetchTimer.updateTime();

    if (fetchTimer.expired() && phase == FetchPhase::UPLOADING_CRASH_RECORDS) {
        // The player is already fetched; unsent records stay pending.
        LOG_W(TAG, "Crash upload timer expired");
        finishCrashUpload();
    } else if (fetchTimer.expired()) {
        LOG_W(TAG, "User data fetch timer expired");
        transitionToConfirmOfflineState = true;
    } else if (fetchTimer.isRunning()) {
        if(SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            showLoadingGlyphs(pdn);
        }
    }
}   

void FetchUserDataState::onStateDismounted(PDN* pdn) {
    LOG_I(TAG, "State dismounted");
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    phase = FetchPhase::IDLE;
    // Requests can outlive the state; their answers no longer advance the flow.
    currentRequestId++;
    transitionToConfirmOfflineState = false;
    transitionToWelcomeMessageState = false;
    transitionToPlayerRegistrationState = false;
    fetchTimer.invalidate();
}

uint32_t FetchUserDataState::beginRequest() {
    return ++currentRequestId;
}

bool FetchUserDataState::isCurrentRequest(uint32_t requestId) const {
    return requestId == currentRequestId;
}

void FetchUserDataState::uploadMatches() {
    phase = FetchPhase::UPLOADING_MATCHES;
    fetchTimer.setTimer(MATCHES_UPLOAD_TIMEOUT);
    const uint32_t requestId = beginRequest();
    QuickdrawRequests::updateMatches(
        wirelessManager,
        matchManager->toJson(),
        [this, requestId](const std::string& jsonResponse) {
            LOG_I(TAG, "Successfully uploaded matches: %s", jsonResponse.c_str());
            matchManager->clearStorage();
            if (!isCurrentRequest(requestId)) {
                return;
            }
            fetchTimer.invalidate();
            fetchUserData();
        },
        [this, requestId](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to upload matches: %s (code: %d)", 
                error.message.c_str(), static_cast<int>(error.code));
            if (!isCurrentRequest(requestId)) {
                return;
            }
            fetchTimer.invalidate();
            fetchUserData();
        });
}

void FetchUserDataState::fetchUserData() {
    phase = FetchPhase::FETCHING_PLAYER;
    fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT);
    const uint32_t requestId = beginRequest();
    QuickdrawRequests::getPlayer(
        wirelessManager,
        player->getUserID(),
        [this, requestId](const PlayerResponse& response) {
            LOG_I(TAG, "Successfully fetched player data: %s (%s)", 
                    response.name.c_str(), response.id.c_str());
            if (!isCurrentRequest(requestId)) {
                return;
            }

            player->setName(response.name.c_str());
            player->setIsHunter(response.isHunter);
            player->setAllegiance(response.allegiance);
            player->setFaction(response.faction.c_str());

            fetchTimer.invalidate();
            if (crashRecordSource != nullptr) {
                // The server just answered, so this is when an upload is most
                // likely to get through.
                phase = FetchPhase::UPLOADING_CRASH_RECORDS;
                fetchTimer.setTimer(CRASH_UPLOAD_TIMEOUT);
                uploadNextCrashRecord();
            } else {
                phase = FetchPhase::IDLE;
                transitionToWelcomeMessageState = true;
            }
        },
        [this, requestId](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to fetch player data: %s (code: %d)", error.message.c_str(),
                  static_cast<int>(error.code));
            if (!isCurrentRequest(requestId)) {
                return;
            }
            phase = FetchPhase::IDLE;
            player->setName("Unknown");
            player->setAllegiance("None");
            transitionToConfirmOfflineState = true;
        });
}

void FetchUserDataState::uploadNextCrashRecord() {
    CrashRecord record{};
    if (!crashRecordSource->loadOldestPending(record)) {
        finishCrashUpload();
        return;
    }

    uint8_t encoded[CrashEncoder::MAX_ENCODED_SIZE];
    const size_t length = CrashEncoder::encode(record, wirelessManager->getMacAddress(), encoded, sizeof(encoded));
    if (length == 0) {
        LOG_E(TAG, "Failed to encode crash #%lu", static_cast<unsigned long>(record.crashNumber));
        finishCrashUpload();
        return;
    }

    const uint32_t crashNumber = record.crashNumber;
    const uint32_t requestId = beginRequest();
    QuickdrawRequests::uploadCrashRecord(
        wirelessManager,
        std::string(reinterpret_cast<const char*>(encoded), length),
        [this, crashNumber, requestId](const std::string& response) {
            (void)response;
            // A 2xx is the only proof the server has the record, and it holds
            // even when the answer arrives after the state has moved on.
            crashRecordSource->markUploaded(crashNumber);
            LOG_I(TAG, "Uploaded crash #%lu", static_cast<unsigned long>(crashNumber));
            if (isCurrentRequest(requestId)) {
                uploadNextCrashRecord();
            }
        },
        [this, crashNumber, requestId](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to upload crash #%lu: %s (code: %d)", static_cast<unsigned long>(crashNumber),
                  error.message.c_str(), static_cast<int>(error.code));
            if (isCurrentRequest(requestId)) {
                finishCrashUpload();
            }
        });
}

void FetchUserDataState::finishCrashUpload() {
    phase = FetchPhase::IDLE;
    // An upload still in flight may land, but must not start the next one.
    currentRequestId++;
    fetchTimer.invalidate();
    transitionToWelcomeMessageState = true;
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