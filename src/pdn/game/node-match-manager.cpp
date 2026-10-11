#include "game/node-match-manager.hpp"

#include <ArduinoJson.h>
#include <cstring>

#include "device/drivers/logger.hpp"
#include "id-generator.hpp"
#include "wireless/mac-functions.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"

static constexpr const char* PREF_COUNT_KEY = "count";
static constexpr const char* PREF_MATCH_KEY = "match_";
static constexpr uint8_t MAX_MATCHES = 255;

#define NODE_MATCH_MANAGER_TAG "NODE_MATCH_MGR"

NodeMatchManager::NodeMatchManager() = default;

NodeMatchManager::~NodeMatchManager() {
    quickdrawWirelessManager = nullptr;
    if (storage) {
        storage->end();
    }
}

void NodeMatchManager::setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc) {
    rdc_ = rdc;
}

void NodeMatchManager::clearCurrentMatch() {
    if (activeDuelState.match) {
        LOG_I(NODE_MATCH_MANAGER_TAG, "Clearing current match");
        activeDuelState.match.reset();
        activeDuelState.hasReceivedDrawResult = false;
        activeDuelState.hasPressedButton = false;
        activeDuelState.duelLocalStartTime = 0;
        activeDuelState.gracePeriodExpiredNoResult = false;
        activeDuelState.opponentNeverPressed = false;
        activeDuelState.buttonMasherCount = 0;
        activeDuelState.matchIsReady = false;
        activeDuelState.duelLinkSide = DuelLinkSide::Responder;
    }
}

void NodeMatchManager::noteOpponentMac(const uint8_t* opponentMac) {
    if (!opponentMac) {
        return;
    }
    if (hasLastOpponentMac_ && memcmp(lastOpponentMac_.data(), opponentMac, 6) != 0) {
        failedAttempts_ = 0;
    }
    memcpy(lastOpponentMac_.data(), opponentMac, 6);
    hasLastOpponentMac_ = true;
}

void NodeMatchManager::primeMatch(const char* matchId, const uint8_t* opponentMac) {
    noteOpponentMac(opponentMac);
    const bool localInHunterSlot = localSideIsInitiator();
    activeDuelState.match.emplace(matchId, player->getUserID().c_str(), localInHunterSlot);
    memcpy(activeDuelState.opponentMac.data(), opponentMac, 6);
}

void NodeMatchManager::initializeMatch(uint8_t* opponentMac) {
    if (activeDuelState.match.has_value()) {
        return;
    }

    activeDuelState.duelLinkSide = DuelLinkSide::Initiator;

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(NODE_MATCH_MANAGER_TAG, "TIMING initializeMatch T=%lu", clock ? clock->milliseconds() : 0UL);

    primeMatch(IdGenerator::getInstance().generateId(), opponentMac);
    sendMatchId();
}

void NodeMatchManager::sendMatchId() {
    QuickdrawCommand command(
        activeDuelState.opponentMac.data(),
        QDCommand::SEND_MATCH_ID,
        activeDuelState.match->getMatchId(),
        player->getUserID().c_str(),
        0,
        wireFlagForLocalSide());
    quickdrawWirelessManager->broadcastPacket(activeDuelState.opponentMac.data(), command);
}

void NodeMatchManager::receiveMatch(
    const char* matchId,
    const char* opponentId,
    bool opponentIsInitiator,
    uint8_t* opponentMac) {
    if (activeDuelState.match.has_value()) {
        return;
    }

    activeDuelState.duelLinkSide = DuelLinkSide::Responder;
    activeDuelState.match.emplace(matchId, player->getUserID().c_str(), false);

    if (opponentIsInitiator) {
        activeDuelState.match->setHunterId(opponentId);
    } else {
        activeDuelState.match->setBountyId(opponentId);
    }
    memcpy(activeDuelState.opponentMac.data(), opponentMac, 6);

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(NODE_MATCH_MANAGER_TAG, "TIMING receiveMatch-ready T=%lu", clock ? clock->milliseconds() : 0UL);

    activeDuelState.matchIsReady = true;
    sendMatchAck();
}

void NodeMatchManager::setDuelLocalStartTime(unsigned long localStartTimeMs) {
    activeDuelState.duelLocalStartTime = localStartTimeMs;
}

unsigned long NodeMatchManager::getDuelLocalStartTime() {
    return activeDuelState.duelLocalStartTime;
}

bool NodeMatchManager::matchResultsAreIn() {
    if (!activeDuelState.match) {
        return false;
    }

    return (activeDuelState.hasReceivedDrawResult && activeDuelState.hasPressedButton)
        || (activeDuelState.opponentNeverPressed && activeDuelState.hasPressedButton)
        || (activeDuelState.gracePeriodExpiredNoResult && !activeDuelState.hasPressedButton);
}

void NodeMatchManager::setNeverPressed() {
    activeDuelState.gracePeriodExpiredNoResult = true;
}

unsigned long NodeMatchManager::syncDeltaMs() const {
    if (!activeDuelState.match) {
        return 0;
    }
    unsigned long hunterTime = activeDuelState.match->getHunterDrawTime();
    unsigned long bountyTime = activeDuelState.match->getBountyDrawTime();
    return hunterTime >= bountyTime ? hunterTime - bountyTime : bountyTime - hunterTime;
}

bool NodeMatchManager::didSync() {
    if (!activeDuelState.match) {
        return false;
    }
    if (activeDuelState.opponentNeverPressed) {
        return false;
    }
    if (activeDuelState.gracePeriodExpiredNoResult) {
        return false;
    }
    if (!activeDuelState.hasPressedButton || !activeDuelState.hasReceivedDrawResult) {
        return false;
    }
    return syncDeltaMs() <= NODE_SYNC_WINDOW_MS;
}

bool NodeMatchManager::linkFailed() {
    return matchResultsAreIn() && !didSync();
}

void NodeMatchManager::recordLinkOutcome() {
    if (!matchResultsAreIn()) {
        return;
    }
    if (didSync()) {
        failedAttempts_ = 0;
    } else {
        failedAttempts_++;
    }
}

bool NodeMatchManager::finalizeMatch() {
    if (!activeDuelState.match) {
        LOG_E(NODE_MATCH_MANAGER_TAG, "Cannot finalize match - no active match");
        return false;
    }

    if (!didSync()) {
        LOG_W(NODE_MATCH_MANAGER_TAG, "Refusing finalize — link did not sync");
        return false;
    }

    activeDuelState.match->setNetworkEdgeMetadata(true, syncDeltaMs(), failedAttempts_);

    if (localSideIsInitiator()) {
        lastMatchDisplay_.myTimeMs = activeDuelState.match->getHunterDrawTime();
        lastMatchDisplay_.opponentTimeMs = activeDuelState.match->getBountyDrawTime();
    } else {
        lastMatchDisplay_.myTimeMs = activeDuelState.match->getBountyDrawTime();
        lastMatchDisplay_.opponentTimeMs = activeDuelState.match->getHunterDrawTime();
    }
    lastMatchDisplay_.hasData = true;

    std::string matchId = activeDuelState.match->getMatchId();

    if (appendMatchToStorage(&*activeDuelState.match)) {
        updateStoredMatchCount(getStoredMatchCount() + 1);
        failedAttempts_ = 0;
        clearCurrentMatch();
        LOG_I(NODE_MATCH_MANAGER_TAG, "Successfully finalized match %s", matchId.c_str());
        return true;
    }

    LOG_E(NODE_MATCH_MANAGER_TAG, "Failed to finalize match %s", matchId.c_str());
    return false;
}

bool NodeMatchManager::getHasReceivedDrawResult() {
    return activeDuelState.hasReceivedDrawResult;
}

bool NodeMatchManager::getHasPressedButton() {
    return activeDuelState.hasPressedButton;
}

void NodeMatchManager::setReceivedDrawResult() {
    activeDuelState.hasReceivedDrawResult = true;
}

parameterizedCallbackFunction NodeMatchManager::getDuelButtonPush() {
    return duelButtonPush;
}

void NodeMatchManager::setReceivedButtonPush() {
    activeDuelState.hasPressedButton = true;
}

bool NodeMatchManager::setHunterDrawTime(unsigned long hunterTimeMs) {
    if (!activeDuelState.match) {
        return false;
    }
    activeDuelState.match->setHunterDrawTime(hunterTimeMs);
    return true;
}

bool NodeMatchManager::setBountyDrawTime(unsigned long bountyTimeMs) {
    if (!activeDuelState.match) {
        return false;
    }
    activeDuelState.match->setBountyDrawTime(bountyTimeMs);
    return true;
}

std::string NodeMatchManager::toJson() {
    JsonDocument doc;
    JsonArray matchArray = doc["matches"].to<JsonArray>();

    uint8_t count = getStoredMatchCount();
    for (uint8_t i = 0; i < count; i++) {
        Match* match = readMatchFromStorage(i);
        if (match) {
            std::string matchJson = match->toJson();
            JsonDocument matchDoc;
            deserializeJson(matchDoc, matchJson);
            matchArray.add(matchDoc.as<JsonObject>());
            delete match;
        }
    }

    std::string output;
    serializeJson(doc, output);
    return output;
}

void NodeMatchManager::clearStorage() {
    storage->clear(NODE_MATCHES_PREFS_NAMESPACE);
    updateStoredMatchCount(0);
}

size_t NodeMatchManager::getStoredMatchCount() {
    return storage->readUChar(NODE_MATCHES_PREFS_NAMESPACE, PREF_COUNT_KEY, 0);
}

bool NodeMatchManager::appendMatchToStorage(const Match* match) {
    if (!match) {
        return false;
    }

    uint8_t count = getStoredMatchCount();
    if (count >= MAX_MATCHES) {
        return false;
    }

    std::string matchJson = match->toJson();
    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, count);

    if (storage->write(NODE_MATCHES_PREFS_NAMESPACE, key, matchJson) != matchJson.length()) {
        return false;
    }
    return true;
}

void NodeMatchManager::updateStoredMatchCount(uint8_t count) {
    storage->writeUChar(NODE_MATCHES_PREFS_NAMESPACE, PREF_COUNT_KEY, count);
}

Match* NodeMatchManager::readMatchFromStorage(uint8_t index) {
    if (index >= getStoredMatchCount()) {
        return nullptr;
    }

    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, index);

    std::string matchJson = storage->read(NODE_MATCHES_PREFS_NAMESPACE, key, "");
    if (matchJson.empty()) {
        return nullptr;
    }

    Match* match = new Match();
    match->fromJson(matchJson);
    return match;
}

parameterizedCallbackFunction NodeMatchManager::getButtonMasher() {
    return buttonMasher;
}

void NodeMatchManager::initialize(
    Player* playerParam,
    StorageInterface* storageParam,
    QuickdrawWirelessManager* quickdrawWirelessManagerParam) {
    player = playerParam;
    storage = storageParam;
    quickdrawWirelessManager = quickdrawWirelessManagerParam;

    duelButtonPush = [](void* ctx) {
        if (!ctx) {
            return;
        }

        auto* clock = SimpleTimer::getPlatformClock();
        if (!clock) {
            return;
        }
        unsigned long now = clock->milliseconds();

        NodeMatchManager* matchManager = static_cast<NodeMatchManager*>(ctx);
        NodeActiveDuelState* activeDuelState = &matchManager->activeDuelState;
        Player* playerLocal = matchManager->player;
        QuickdrawWirelessManager* wirelessManager = matchManager->quickdrawWirelessManager;

        if (matchManager->getHasPressedButton()) {
            return;
        }

        if (!matchManager->getCurrentMatch().has_value()) {
            return;
        }

        unsigned long reactionTimeMs = now - matchManager->getDuelLocalStartTime();

        if (activeDuelState->buttonMasherCount > 0) {
            reactionTimeMs = reactionTimeMs + activeDuelState->calculateButtonMasherPenalty();
        }

        if (matchManager->localSideIsInitiator()) {
            matchManager->setHunterDrawTime(reactionTimeMs);
        } else {
            matchManager->setBountyDrawTime(reactionTimeMs);
        }

        playerLocal->addReactionTime(reactionTimeMs);

        QuickdrawCommand command(
            activeDuelState->opponentMac.data(),
            QDCommand::DRAW_RESULT,
            matchManager->getCurrentMatch()->getMatchId(),
            playerLocal->getUserID().c_str(),
            static_cast<long>(reactionTimeMs),
            matchManager->wireFlagForLocalSide());

        wirelessManager->broadcastPacket(activeDuelState->opponentMac.data(), command);
        matchManager->setReceivedButtonPush();
    };

    buttonMasher = [](void* ctx) {
        NodeMatchManager* matchManager = static_cast<NodeMatchManager*>(ctx);
        matchManager->activeDuelState.buttonMasherCount++;
    };
}

void NodeMatchManager::sendMatchAck() {
    QuickdrawCommand command(
        activeDuelState.opponentMac.data(),
        QDCommand::MATCH_ID_ACK,
        activeDuelState.match->getMatchId(),
        player->getUserID().c_str(),
        0,
        wireFlagForLocalSide());
    quickdrawWirelessManager->broadcastPacket(activeDuelState.opponentMac.data(), command);
}

bool NodeMatchManager::isFromActiveMatchOpponent(const QuickdrawCommand& command) const {
    if (!activeDuelState.match.has_value()) {
        return false;
    }
    if (memcmp(command.wifiMacAddr, activeDuelState.opponentMac.data(), 6) != 0) {
        return false;
    }
    if (strcmp(command.matchId, activeDuelState.match->getMatchId()) != 0) {
        return false;
    }
    return true;
}

void NodeMatchManager::listenForMatchEvents(const QuickdrawCommand& command) {
    if (command.command == QDCommand::SEND_MATCH_ID) {
        if (!rdc_ || !rdc_->isDirectPeer(command.wifiMacAddr)) {
            LOG_W(NODE_MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        receiveMatch(
            command.matchId,
            command.playerId,
            command.isHunter,
            const_cast<uint8_t*>(command.wifiMacAddr));
    } else if (command.command == QDCommand::MATCH_ID_ACK) {
        if (!isFromActiveMatchOpponent(command)) {
            return;
        }
        if (localSideIsInitiator()) {
            activeDuelState.match->setBountyId(command.playerId);
        } else {
            activeDuelState.match->setHunterId(command.playerId);
        }
        activeDuelState.matchIsReady = true;
    } else if (command.command == QDCommand::DRAW_RESULT || command.command == QDCommand::NEVER_PRESSED) {
        if (!isFromActiveMatchOpponent(command)) {
            return;
        }

        long opponentTime = command.playerDrawTime;

        if (command.isHunter) {
            setHunterDrawTime(opponentTime);
        } else {
            setBountyDrawTime(opponentTime);
        }

        if (command.command == QDCommand::NEVER_PRESSED) {
            activeDuelState.opponentNeverPressed = true;
        }

        setReceivedDrawResult();
    } else if (command.command == QDCommand::MATCH_ROLE_MISMATCH) {
        LOG_W(NODE_MATCH_MANAGER_TAG, "Ignoring MATCH_ROLE_MISMATCH in node mode");
    } else {
        LOG_W(NODE_MATCH_MANAGER_TAG, "Unexpected command: %d", command.command);
    }
}

void NodeMatchManager::sendNeverPressed(unsigned long pityTime) {
    if (!activeDuelState.match.has_value()) {
        return;
    }

    if (localSideIsInitiator()) {
        setHunterDrawTime(pityTime);
    } else {
        setBountyDrawTime(pityTime);
    }
    setNeverPressed();
    player->addReactionTime(pityTime);

    QuickdrawCommand command(
        activeDuelState.opponentMac.data(),
        QDCommand::NEVER_PRESSED,
        activeDuelState.match->getMatchId(),
        player->getUserID().c_str(),
        static_cast<long>(pityTime),
        wireFlagForLocalSide());
    quickdrawWirelessManager->broadcastPacket(activeDuelState.opponentMac.data(), command);
}

bool NodeMatchManager::isMatchReady() {
    return activeDuelState.matchIsReady;
}
