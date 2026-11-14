#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/match-packet.hpp"
#include "game/posse-manager.hpp"
#include "game/record-tag.hpp"
#include "game/shootout-manager.hpp"
#include "id-generator.hpp"
#include "utils/hardware-random.hpp"
#include "utils/secure-wipe.hpp"
#include <ArduinoJson.h>
#include <algorithm>
#include <optional>
#include <cstring>

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

namespace {

static_assert(MATCH_KEY_FRAME_KEY_OFFSET + MATCH_KEY_SIZE == APPLICATION_PAYLOAD_LEN,
              "the match key frame is the match ID and the key, filling the payload");

// The decoded form of a match frame from @p mac, boost included.
MatchCommand commandFromPacket(const uint8_t* mac, const MatchPacket& packet) {
    MatchCommand command(mac, packet.command, packet.matchId, packet.playerDrawTime,
                         packet.isHunter, packet.playerUuid, packet.teamUuid);
    command.boostMs = packet.boostMs;
    return command;
}

bool isShootoutMatchId(const char* matchId) {
    return strncmp(matchId, kShootoutMatchIdPrefix, sizeof(kShootoutMatchIdPrefix) - 1) == 0;
}

void matchSlotKey(uint32_t slot, char (&key)[16]) {
    snprintf(key, sizeof(key), "%s%u", PREF_MATCH_KEY_PREFIX, static_cast<unsigned>(slot));
}

// A string of at most `size` characters, copied into `out` without a terminator
// when it fills it; `out` must start zeroed.
bool copyTextField(JsonVariantConst value, char* out, size_t size) {
    if (!value.is<const char*>()) {
        return false;
    }
    const char* text = value.as<const char*>();
    const size_t length = strlen(text);
    if (length > size) {
        return false;
    }
    memcpy(out, text, length);
    return true;
}

// Builds a LEGACY_MATCH record from what older firmware's Match::toJson() stored.
// A field missing, mistyped or too long refuses the whole string, leaving
// @p content untouched, so nothing it held is sent half-read.
bool parseLegacyJsonMatch(const std::string& json, MatchRecordContent& content) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return false;
    }
    LegacyMatchContent legacy{};
    if (!copyTextField(doc["match_id"], legacy.matchId, sizeof(legacy.matchId)) ||
        !copyTextField(doc["hunter"], legacy.hunterPin, sizeof(legacy.hunterPin)) ||
        !copyTextField(doc["bounty"], legacy.bountyPin, sizeof(legacy.bountyPin)) ||
        !doc["hunter_time"].is<uint32_t>() || !doc["bounty_time"].is<uint32_t>() ||
        !doc["winner_is_hunter"].is<bool>()) {
        return false;
    }
    legacy.hunterTimeMs = doc["hunter_time"].as<uint32_t>();
    legacy.bountyTimeMs = doc["bounty_time"].as<uint32_t>();
    legacy.winnerIsHunter = doc["winner_is_hunter"].as<bool>() ? 1 : 0;
    memcpy(&content, &legacy, sizeof(content));
    return true;
}

}  // namespace

MatchManager::MatchManager(Player* player, const RecordKey* recordKey, StorageInterface* storage,
                           WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
    : player(player)
    , recordKey(recordKey)
    , rdc(rdc)
    , storage(storage)
    , matchChannel(wirelessManager, PktType::kMatchCommand) {
    matchChannel.setAdmitFilter(
    [this](const uint8_t* fromMac, const MatchPacket& p) { return admitMatchFrame(fromMac, p); });
    matchChannel.onReceive([this](const uint8_t* fromMac, const MatchPacket& p) {
        if (p.command == MatchCmd::SEND_MATCH_ID) {
            onMatchInitiation(fromMac, p);
            return;
        }
        listenForMatchEvents(commandFromPacket(fromMac, p));
    });
    // The bounty readies on the radio's SEND_SUCCESS for its MATCH_ID_ACK, not on
    // sending it, so a lost ack no longer starts its countdown ahead of the hunter's.
    matchChannel.setOnDelivered([this](uint8_t seqId, const uint8_t*) {
        if (activeMatchState.pendingAckSeqId == 0 || seqId != activeMatchState.pendingAckSeqId) {
            return;
        }
        activeMatchState.pendingAckSeqId = 0;
        activeMatchState.matchIsReady = true;
    });
    // No delivery report within the retransmit span: drop the match rather than
    // count down against a hunter that may never have heard the ack.
    matchChannel.setOnAbandon([this](uint8_t seqId, const uint8_t*, const MatchPacket&) {
        if (activeMatchState.pendingAckSeqId == 0 || seqId != activeMatchState.pendingAckSeqId) {
            return;
        }
        clearCurrentMatch();
    });
    if (rdc != nullptr) {
        rdc->setOnApplicationFrame(
        [this](SerialIdentifier jack, ApplicationFrameKind kind,
               const ApplicationPayload& payload) { onApplicationFrame(jack, kind, payload); },
        this);
        rdc->setOnJackChange(
        [this](SerialIdentifier jack, bool connected) { onJackChange(jack, connected); }, this);
    }
    initialize();
}

uint8_t MatchManager::sendCommand(const uint8_t* mac, int command, const char* matchId,
                                  long drawTime, unsigned long boostMs, bool isHunter) {
    MatchPacket packet{};
    packet.command = command;
    packet.playerDrawTime = drawTime;
    packet.boostMs = static_cast<uint32_t>(boostMs);
    packet.isHunter = isHunter;
    IdGenerator::copyId(packet.matchId, matchId);
    packet.playerUuid = player->getServerPlayerId();
    packet.teamUuid = player->getRegistrationTeam().id;
    if (activeMatchState.matchKeyIsHeld) {
        packet.counter = ++activeMatchState.txCounter;
        signMatchPacket(activeMatchState.matchKey, packet);
    }

    LOG_I(MATCH_MANAGER_TAG, "Sending command %i to %s", command, MacToString(mac));
    return matchChannel.sendReliable(mac, packet);
}

MatchManager::~MatchManager() {
    if (rdc != nullptr) {
        rdc->setOnApplicationFrame(nullptr, this);
        rdc->setOnJackChange(nullptr, this);
    }
    secureWipe(activeMatchState.matchKey.data(), activeMatchState.matchKey.size());
    wipePendingMatchKey();
    if (storage) {
        storage->end();
    }
}

void MatchManager::setBoostProvider(std::function<unsigned long()> provider) {
    boostProvider = std::move(provider);
}

void MatchManager::setShootoutManager(ShootoutManager* manager) { shootoutManager = manager; }

bool MatchManager::currentMatchIsShootout() const {
    return activeMatchState.match.has_value() &&
           isShootoutMatchId(activeMatchState.match->getMatchId());
}

void MatchManager::clearShootoutMatch() {
    if (!currentMatchIsShootout()) return;
    clearCurrentMatch();
}

void MatchManager::clearCurrentMatch() {
    if (!activeMatchState.match) return;
    LOG_I(MATCH_MANAGER_TAG, "Clearing current match");
    // Keep matchChannel retrying only when a local DRAW_RESULT or NEVER_PRESSED was sent.
    if (!activeMatchState.hasPressedButton && !activeMatchState.gracePeriodExpiredNoResult) {
        matchChannel.cancel(activeMatchState.opponentMac.data());
    }
    // Retransmits that outlive the match were signed when queued and need no key.
    secureWipe(activeMatchState.matchKey.data(), activeMatchState.matchKey.size());
    activeMatchState = ActiveMatchState{};
}

void MatchManager::primeMatch(const char* matchId, const uint8_t* opponentMac,
                              bool localIsHunter) {
    activeMatchState.localIsHunter = localIsHunter;
    activeMatchState.match.emplace(matchId);
    memcpy(activeMatchState.opponentMac.data(), opponentMac, 6);

    Match& match = *activeMatchState.match;
    const UUIDBytes& myPlayerUuid = player->getServerPlayerId();
    const UUIDBytes& myTeamUuid = player->getRegistrationTeam().id;
    if (localIsHunter) {
        match.setHunterPlayerId(myPlayerUuid);
        match.setHunterTeamId(myTeamUuid);
    } else {
        match.setBountyPlayerId(myPlayerUuid);
        match.setBountyTeamId(myTeamUuid);
    }
    matchStartedCallbacks.fire();
}

void MatchManager::setOpponentIdentity(bool opponentIsHunter, const UUIDBytes& playerUuid,
                                       const UUIDBytes& teamUuid) {
    Match& match = *activeMatchState.match;
    if (opponentIsHunter) {
        match.setHunterPlayerId(playerUuid);
        match.setHunterTeamId(teamUuid);
    } else {
        match.setBountyPlayerId(playerUuid);
        match.setBountyTeamId(teamUuid);
    }
}

void MatchManager::installMatchKey(const MatchKey& key, uint32_t opponentCounter) {
    activeMatchState.matchKey = key;
    activeMatchState.matchKeyIsHeld = true;
    activeMatchState.txCounter = 0;
    activeMatchState.rxCounter = opponentCounter;
}

void MatchManager::wipePendingMatchKey() {
    if (pendingMatchKey.has_value()) {
        secureWipe(pendingMatchKey->key.data(), pendingMatchKey->key.size());
    }
    pendingMatchKey.reset();
}

SerialIdentifier MatchManager::opponentJack() const {
    return PosseManager::opponentJackFor(player->isHunter());
}

void MatchManager::initializeMatch(uint8_t* opponentMac) {
    if (activeMatchState.match.has_value()) {
        return;
    }

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING initializeMatch T=%lu", clock ? clock->milliseconds() : 0UL);

    if (rdc == nullptr) {
        LOG_E(MATCH_MANAGER_TAG, "No coordinator to carry the match key; no match started");
        return;
    }
    const char* matchId = IdGenerator::getInstance().generateId();
    UUIDBytes matchIdBytes;
    if (!UUIDBytes::parse(matchId, matchIdBytes)) {
        LOG_E(MATCH_MANAGER_TAG, "Generated match ID is not a UUID; no match started");
        return;
    }

    // Drawn fresh for every match, so no frame of one match verifies in another.
    MatchKey key;
    fillHardwareRandom(key.data(), key.size());
    ApplicationPayload keyFrame;
    memcpy(keyFrame.data() + MATCH_KEY_FRAME_ID_OFFSET, matchIdBytes.bytes, UUIDBytes::SIZE);
    memcpy(keyFrame.data() + MATCH_KEY_FRAME_KEY_OFFSET, key.data(), key.size());
    // Queued before the initiation goes out: the Bounty pairs the two, and the
    // radio frame overtakes the serial one either way.
    const bool accepted =
    rdc->sendApplicationFrame(opponentJack(), ApplicationFrameKind::MATCH_KEY, keyFrame);
    secureWipe(keyFrame.data(), keyFrame.size());
    if (!accepted) {
        LOG_E(MATCH_MANAGER_TAG, "Opponent jack refused the match key; no match started");
        secureWipe(key.data(), key.size());
        return;
    }

    primeMatch(matchId, opponentMac, player->isHunter());
    installMatchKey(key, 0);
    secureWipe(key.data(), key.size());
    sendCommand(activeMatchState.opponentMac.data(), MatchCmd::SEND_MATCH_ID,
                activeMatchState.match->getMatchId(), 0, 0, activeMatchState.localIsHunter);
}

void MatchManager::initializeShootoutMatch(const char* matchId, const uint8_t* opponentMac,
                                           bool localIsHunter) {
    // A repeat MATCH_START names a match that may already be mounted; the live one
    // wins, since re-priming would reset a draw already in progress.
    if (activeMatchState.match.has_value()) return;

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING initializeShootoutMatch T=%lu",
          clock ? clock->milliseconds() : 0UL);

    primeMatch(matchId, opponentMac, localIsHunter);
    activeMatchState.matchIsReady = true;
}

void MatchManager::receiveMatch(const char* matchId, bool opponentIsHunter, uint8_t* opponentMac,
                                const MatchKey& matchKey, uint32_t opponentCounter,
                                const UUIDBytes& opponentPlayerUuid,
                                const UUIDBytes& opponentTeamUuid) {
    primeMatch(matchId, opponentMac, player->isHunter());
    installMatchKey(matchKey, opponentCounter);
    setOpponentIdentity(opponentIsHunter, opponentPlayerUuid, opponentTeamUuid);

    auto* clock = SimpleTimer::getPlatformClock();
    LOG_D(MATCH_MANAGER_TAG, "TIMING receiveMatch-ackSent T=%lu",
          clock ? clock->milliseconds() : 0UL);

    activeMatchState.pendingAckSeqId =
    sendCommand(activeMatchState.opponentMac.data(), MatchCmd::MATCH_ID_ACK,
                activeMatchState.match->getMatchId(), 0, 0, activeMatchState.localIsHunter);
}

void MatchManager::setMatchLocalStartTime(unsigned long localStartTimeMs) {
    activeMatchState.matchLocalStartTime = localStartTimeMs;
    if (!activeMatchState.earlyOpponentResult.has_value()) return;
    const OpponentResult held = *activeMatchState.earlyOpponentResult;
    activeMatchState.earlyOpponentResult.reset();
    LOG_I(MATCH_MANAGER_TAG, "Applying the draw result held from before the draw window");
    applyOpponentResult(held);
}

unsigned long MatchManager::getMatchLocalStartTime() const {
    return activeMatchState.matchLocalStartTime.value_or(0);
}

bool MatchManager::matchResultsAreIn() {
    if (!activeMatchState.match) {
        return false;
    }

    // Final clause is the deadlock escape for both-timed-out matches where the
    // opponent's NEVER_PRESSED packet drops.
    return (activeMatchState.hasReceivedDrawResult && activeMatchState.hasPressedButton) ||
           (activeMatchState.opponentNeverPressed && activeMatchState.hasPressedButton) ||
           activeMatchState.gracePeriodExpiredNoResult;
}

void MatchManager::setNeverPressed() { activeMatchState.gracePeriodExpiredNoResult = true; }

bool MatchManager::didWin() {
    if (!activeMatchState.match) {
        return false;
    }

    if (activeMatchState.opponentNeverPressed) {
        return true;
    }
    if (activeMatchState.gracePeriodExpiredNoResult) {
        return false;
    }

    // An opponent whose result never arrived has no time to beat; a boosted
    // local time below zero must not win against the missing one.
    if (!activeMatchState.hasReceivedDrawResult) {
        return false;
    }
    return getEffectiveDrawTime(activeMatchState.localIsHunter) <
           getEffectiveDrawTime(!activeMatchState.localIsHunter);
}

bool MatchManager::finalizeMatch() {
    if (!activeMatchState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    Match& match = *activeMatchState.match;
    const long hunterTime = getEffectiveDrawTime(true);
    const long bountyTime = getEffectiveDrawTime(false);

    // The whole result-screen snapshot is taken here, before clearCurrentMatch wipes the match.
    lastMatchDisplay.myTimeMs = activeMatchState.localIsHunter ? hunterTime : bountyTime;
    lastMatchDisplay.opponentTimeMs = activeMatchState.localIsHunter ? bountyTime : hunterTime;
    lastMatchDisplay.boostMs = activeMatchState.boostMs;
    lastMatchDisplay.penaltyMs = activeMatchState.penaltyMs;
    lastMatchDisplay.hasData = true;

    // The recorded winner must be one both devices arrive at, so it cannot come
    // from this device's own didWin(). Each knows which side never pressed, and
    // that outranks the times as it does on the result screen; otherwise the
    // faster effective draw wins, the same comparison didWin() makes. A tie,
    // neither side pressing, or an opponent result that never arrived leaves
    // nothing both can report, and the server requires a winner.
    const bool localNeverPressed = activeMatchState.gracePeriodExpiredNoResult;
    const bool opponentNeverPressed = activeMatchState.opponentNeverPressed;
    bool hunterWon = hunterTime < bountyTime;
    bool winnerIsShared = activeMatchState.hasReceivedDrawResult && hunterTime != bountyTime;
    if (localNeverPressed != opponentNeverPressed) {
        hunterWon = opponentNeverPressed == activeMatchState.localIsHunter;
        winnerIsShared = true;
    } else if (localNeverPressed) {
        winnerIsShared = false;
    }
    match.setWinnerPlayerId(hunterWon ? match.getHunterPlayerId() : match.getBountyPlayerId());
    if (PlatformClock* clock = SimpleTimer::getPlatformClock()) {
        match.setOccurredAtUnixMs(clock->unixMilliseconds());
    }

    std::string matchId = match.getMatchId();

    // Shootout matches are local-ephemeral: no save, no upload. Neither is a match
    // with a demo player, which has no server identity for the record to name.
    // Nor is a record naming one player in both slots (the server refuses it); the
    // handshake checks should already have refused that opponent.
    if (currentMatchIsShootout() || match.getHunterPlayerId().isZero() ||
        match.getBountyPlayerId().isZero() ||
        match.getHunterPlayerId() == match.getBountyPlayerId() || !winnerIsShared) {
        clearCurrentMatch();
        return true;
    }

    // Tagged once, here, under the key held now; a record that cannot be tagged is not kept.
    MatchRecordContent content{};
    match.serialize(content.recordBytes);
    if (!tagMatchRecord(recordKey, content)) {
        LOG_E(MATCH_MANAGER_TAG, "Match %s not stored: no record key to tag it with",
              matchId.c_str());
        clearCurrentMatch();
        return true;
    }

    if (appendRecordToStorage(content)) {
        clearCurrentMatch();
        LOG_I(MATCH_MANAGER_TAG, "Successfully finalized match %s\n", matchId.c_str());
        return true;
    }

    LOG_E("PDN", "Failed to finalize match %s\n", matchId.c_str());
    return false;
}

bool MatchManager::getHasReceivedDrawResult() const {
    return activeMatchState.hasReceivedDrawResult;
}

bool MatchManager::getHasPressedButton() const { return activeMatchState.hasPressedButton; }

void MatchManager::setReceivedDrawResult() { activeMatchState.hasReceivedDrawResult = true; }

parameterizedCallbackFunction MatchManager::getMatchButtonPush() { return matchButtonPush; }

void MatchManager::setReceivedButtonPush() { activeMatchState.hasPressedButton = true; }

bool MatchManager::setHunterDrawTime(unsigned long hunterTimeMs) {
    if (!activeMatchState.match) {
        return false;
    }
    activeMatchState.match->setHunterDrawTime(hunterTimeMs);
    return true;
}

bool MatchManager::setBountyDrawTime(unsigned long bountyTimeMs) {
    if (!activeMatchState.match) {
        return false;
    }
    activeMatchState.match->setBountyDrawTime(bountyTimeMs);
    return true;
}

long MatchManager::getEffectiveDrawTime(bool hunterSlot) const {
    const Match& match = *activeMatchState.match;
    const unsigned long rawTimeMs =
    hunterSlot ? match.getHunterDrawTime() : match.getBountyDrawTime();
    const unsigned long boostMs = hunterSlot == activeMatchState.localIsHunter
                                  ? activeMatchState.boostMs
                                  : activeMatchState.opponentBoostMs;
    return static_cast<long>(rawTimeMs) - static_cast<long>(boostMs);
}

uint32_t MatchManager::getMatchSequenceNumber() {
    if (!storage) return 0;
    return storage->readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, 0);
}

uint32_t MatchManager::getCollectedHighWaterMark() {
    if (!storage) return 0;
    return storage->readUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY, 0);
}

bool MatchManager::appendRecordToStorage(const MatchRecordContent& content) {
    if (!storage) return false;

    const uint32_t nextSeq = getMatchSequenceNumber() + 1;
    char key[16];
    matchSlotKey((nextSeq - 1) % MATCH_RING_CAPACITY, key);

    if (storage->writeBytes(MATCH_RING_PREFS_NAMESPACE, key,
                            reinterpret_cast<const uint8_t*>(&content),
                            MATCH_SLOT_SIZE) != MATCH_SLOT_SIZE) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to save match binary at key %s", key);
        return false;
    }

    if (storage->writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_SEQ_KEY, nextSeq) != sizeof(uint32_t)) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to update match sequence number to %u", nextSeq);
        return false;
    }

    return true;
}

bool MatchManager::readRingSlot(const char* ns, uint32_t seq, MatchRecordContent& content) {
    char key[16];
    matchSlotKey((seq - 1) % MATCH_RING_CAPACITY, key);
    MatchRecordContent slot{};
    const size_t length =
    storage->readBytes(ns, key, reinterpret_cast<uint8_t*>(&slot), sizeof(slot));
    // Only older firmware wrote untagged slots; this firmware's own ring never holds one.
    if (length != MATCH_SLOT_SIZE && length != LEGACY_UNTAGGED_SLOT_SIZE) {
        return false;
    }
    content = slot;
    return true;
}

bool MatchManager::readLowestPendingInRing(const char* ns, uint32_t& highestReported,
                                           uint32_t& recordNumber, MatchRecordContent& content) {
    return findLowestReadableRecord(
    storage->readUInt(ns, PREF_COLLECTED_SEQ_KEY, 0), storage->readUInt(ns, PREF_SEQ_KEY, 0),
    MATCH_RING_CAPACITY, highestReported, recordNumber,
    [this, ns, &content](uint32_t seq) { return readRingSlot(ns, seq, content); },
    [ns](uint32_t seq) {
        LOG_W(MATCH_MANAGER_TAG, "Stored match %u in %s is not a readable record",
              static_cast<unsigned>(seq), ns);
    });
}

bool MatchManager::readLowestPendingLegacyJson(uint32_t& recordNumber,
                                               MatchRecordContent& content) {
    const uint32_t count =
    storage->readUChar(LEGACY_MATCHES_PREFS_NAMESPACE, LEGACY_PREF_COUNT_KEY, 0);
    for (uint32_t index = legacyJsonFirstHeld; index < count; index++) {
        char key[16];
        matchSlotKey(index, key);
        const std::string json = storage->read(LEGACY_MATCHES_PREFS_NAMESPACE, key, "");
        if (json.empty()) {
            if (index == legacyJsonFirstHeld) {
                legacyJsonFirstHeld = index + 1;
            }
            continue;
        }
        if (parseLegacyJsonMatch(json, content)) {
            recordNumber = index;
            return true;
        }
        // Kept: erasing it would lose a match nobody has received.
        if (index >= legacyJsonReportedBelow) {
            legacyJsonReportedBelow = index + 1;
            LOG_W(MATCH_MANAGER_TAG, "Older firmware's match %u does not parse; kept unsent",
                  static_cast<unsigned>(index));
        }
    }
    return false;
}

bool MatchManager::readLowestPendingFrame(uint32_t& recordNumber, RecordKind& kind,
                                          MatchRecordContent& content) {
    if (readLowestPendingInRing(MATCH_RING_PREFS_NAMESPACE, highestUnreadableReported, recordNumber,
                                content) ||
        readLowestPendingInRing(LEGACY_MATCHES_PREFS_NAMESPACE, highestLegacyUnreadableReported,
                                recordNumber, content)) {
        kind = RecordKind::MATCH;
        return true;
    }
    if (readLowestPendingLegacyJson(recordNumber, content)) {
        kind = RecordKind::LEGACY_MATCH;
        return true;
    }
    return false;
}

bool MatchManager::ringHoldsPending(const char* ns, uint32_t seq,
                                    const uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
    const uint32_t newest = storage->readUInt(ns, PREF_SEQ_KEY, 0);
    const uint32_t firstPending = firstPendingRecordNumber(
    storage->readUInt(ns, PREF_COLLECTED_SEQ_KEY, 0), newest, MATCH_RING_CAPACITY);
    if (firstPending == 0 || seq < firstPending || seq > newest) {
        return false;
    }
    MatchRecordContent slot{};
    if (!readRingSlot(ns, seq, slot)) {
        return false;
    }
    uint8_t slotCheck[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(slot, slotCheck);
    return memcmp(slotCheck, recordCheck, RECORD_CHECK_SIZE) == 0;
}

void MatchManager::advanceRingMark(const char* ns, uint32_t recordNumber) {
    const uint32_t collected = storage->readUInt(ns, PREF_COLLECTED_SEQ_KEY, 0);
    if (advancedRecordMark(collected, storage->readUInt(ns, PREF_SEQ_KEY, 0), recordNumber) ==
        collected) {
        return;
    }
    if (storage->writeUInt(ns, PREF_COLLECTED_SEQ_KEY, recordNumber) != sizeof(uint32_t)) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to store collected mark %u in %s",
              static_cast<unsigned>(recordNumber), ns);
    }
}

void MatchManager::retireLegacyJsonMatch(uint32_t index,
                                         const uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
    const uint32_t count =
    storage->readUChar(LEGACY_MATCHES_PREFS_NAMESPACE, LEGACY_PREF_COUNT_KEY, 0);
    char key[16];
    matchSlotKey(index, key);
    MatchRecordContent held{};
    uint8_t heldCheck[RECORD_CHECK_SIZE] = {};
    if (index >= count ||
        !parseLegacyJsonMatch(storage->read(LEGACY_MATCHES_PREFS_NAMESPACE, key, ""), held)) {
        LOG_W(MATCH_MANAGER_TAG, "Acknowledged older firmware's match %u is no longer held",
              static_cast<unsigned>(index));
        return;
    }
    computeRecordCheck(held, heldCheck);
    if (memcmp(heldCheck, recordCheck, RECORD_CHECK_SIZE) != 0) {
        LOG_W(MATCH_MANAGER_TAG, "Acknowledged older firmware's match %u differs from the one held",
              static_cast<unsigned>(index));
        return;
    }
    if (!storage->remove(LEGACY_MATCHES_PREFS_NAMESPACE, key)) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to erase delivered older firmware's match %u",
              static_cast<unsigned>(index));
        return;
    }

    // The count and older firmware's write probe go only once no string is left,
    // an unparseable one included: the count is what finds the strings.
    for (uint32_t remaining = legacyJsonFirstHeld; remaining < count; remaining++) {
        matchSlotKey(remaining, key);
        if (!storage->read(LEGACY_MATCHES_PREFS_NAMESPACE, key, "").empty()) {
            return;
        }
    }
    storage->remove(LEGACY_MATCHES_PREFS_NAMESPACE, LEGACY_PREF_COUNT_KEY);
    storage->remove(LEGACY_MATCHES_PREFS_NAMESPACE, LEGACY_PREF_TEST_KEY);
}

void MatchManager::onRecordAcknowledged(RecordKind kind, uint32_t recordNumber,
                                        const uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
    if (kind == RecordKind::LEGACY_MATCH) {
        retireLegacyJsonMatch(recordNumber, recordCheck);
        return;
    }
    if (kind != RecordKind::MATCH) {
        LOG_E(MATCH_MANAGER_TAG, "Acknowledgement of kind %u is not a match record",
              static_cast<unsigned>(kind));
        return;
    }
    // Both rings number from 1, so the record check says which one was stored.
    if (ringHoldsPending(MATCH_RING_PREFS_NAMESPACE, recordNumber, recordCheck)) {
        advanceRingMark(MATCH_RING_PREFS_NAMESPACE, recordNumber);
    } else if (ringHoldsPending(LEGACY_MATCHES_PREFS_NAMESPACE, recordNumber, recordCheck)) {
        advanceRingMark(LEGACY_MATCHES_PREFS_NAMESPACE, recordNumber);
    } else {
        LOG_W(MATCH_MANAGER_TAG, "Acknowledged match %u is no longer pending",
              static_cast<unsigned>(recordNumber));
    }
}

parameterizedCallbackFunction MatchManager::getButtonMasher() {
    return buttonMasher;
}

void MatchManager::clearButtonMasherCount() { activeMatchState.buttonMasherCount = 0; }

void MatchManager::initialize() {
    // An NVS wipe of "seq" alone would otherwise leave every new match marked collected.
    if (getCollectedHighWaterMark() > getMatchSequenceNumber()) {
        storage->writeUInt(MATCH_RING_PREFS_NAMESPACE, PREF_COLLECTED_SEQ_KEY,
                           getMatchSequenceNumber());
    }

    matchButtonPush = [](void* ctx) {
        if (!ctx) {
            LOG_E(MATCH_MANAGER_TAG, "Button press handler received null context");
            return;
        }

        auto* clock = SimpleTimer::getPlatformClock();
        if (!clock) {
            LOG_E(MATCH_MANAGER_TAG, "Button press handler called with no platform clock");
            return;
        }
        unsigned long now = clock->milliseconds();

        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        ActiveMatchState* activeMatchState = &matchManager->activeMatchState;
        Player* player = matchManager->player;

        if(matchManager->getHasPressedButton()) {
            LOG_I(MATCH_MANAGER_TAG, "Button already pressed - skipping");
            return;
        }

        if(!matchManager->getCurrentMatch().has_value()) {
            LOG_E(MATCH_MANAGER_TAG, "No current match - skipping");
            return;
        }

        unsigned long reactionTimeMs = now - matchManager->getMatchLocalStartTime();

        const unsigned long penaltyMs = activeMatchState->calculateButtonMasherPenalty();
        reactionTimeMs += penaltyMs;

        // Held to a full posse's worth, the same bound the opponent applies to
        // the boost it reads off the frame, so both compare the same numbers.
        const unsigned long boost =
        std::min(matchManager->boostProvider ? matchManager->boostProvider() : 0UL,
                 PosseManager::getMaxBoostMs());

        // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        activeMatchState->boostMs = boost;
        activeMatchState->penaltyMs = penaltyMs;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms (boost %lu) for %s",
              reactionTimeMs, boost, activeMatchState->localIsHunter ? "Hunter" : "Bounty");

        // The record keeps the raw time; the boost is applied when times are compared.
        activeMatchState->localIsHunter ? matchManager->setHunterDrawTime(reactionTimeMs)
                                        : matchManager->setBountyDrawTime(reactionTimeMs);

        // Player stats record the raw (unboosted) reaction time — boost is a
        // match-time advantage, not an achievement the player actually made.
        player->addReactionTime(reactionTimeMs);

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s",
              MacToString(activeMatchState->opponentMac.data()));

        // The raw time and the boost go out separately: the opponent's copy of
        // the record needs the first, and its comparison needs both.
        matchManager->sendCommand(activeMatchState->opponentMac.data(), MatchCmd::DRAW_RESULT,
                                  matchManager->getCurrentMatch()->getMatchId(), reactionTimeMs,
                                  boost, activeMatchState->localIsHunter);

        matchManager->setReceivedButtonPush();
    };

    buttonMasher = [](void *ctx) {
        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        matchManager->activeMatchState.buttonMasherCount++;
        LOG_I(MATCH_MANAGER_TAG, "Button masher count: %d",
              matchManager->activeMatchState.buttonMasherCount);
    };
}

bool MatchManager::isFromActiveMatchOpponent(const uint8_t* fromMac, const char* matchId) const {
    if (!activeMatchState.match.has_value()) return false;
    if (memcmp(fromMac, activeMatchState.opponentMac.data(), 6) != 0) return false;
    // Bounded: a frame's match ID comes off the wire and need not be terminated.
    return strncmp(matchId, activeMatchState.match->getMatchId(), IdGenerator::UUID_BUFFER_SIZE) ==
           0;
}

void MatchManager::listenForMatchEvents(const MatchCommand& command) {
    LOG_I(MATCH_MANAGER_TAG, "Received command: %d", command.command);

    if (command.command == MatchCmd::SEND_MATCH_ID) {
        LOG_I(MATCH_MANAGER_TAG, "Received SEND_MATCH_ID command from opponent");
        // One match at a time, and a tournament pairs its own players.
        if (activeMatchState.match.has_value()) return;
        if (shootoutManager && shootoutManager->active()) return;
        // Only the cabled peer may start a match; the key off the cable
        // authenticates it (pairInitiationWithKey).
        if (!rdc || !rdc->isDirectPeer(command.wifiMacAddr)) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        if (!player->getServerPlayerId().isZero() &&
            command.playerUuid == player->getServerPlayerId()) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: self playerUuid");
            return;
        }
        if (player->isHunter() == command.isHunter) {
            // Echo the sender's matchId/MAC: activeMatchState.match is still empty.
            sendCommand(command.wifiMacAddr, MatchCmd::MATCH_ROLE_MISMATCH, command.matchId, 0, 0,
                        player->isHunter());
            return;
        }
        // Without the key off the cable the opponent's results could not be
        // told from forgeries, so no match starts.
        if (command.matchKey == nullptr) {
            LOG_I(MATCH_MANAGER_TAG, "SEND_MATCH_ID not verified under a match key; no match yet");
            return;
        }
        receiveMatch(command.matchId, command.isHunter, const_cast<uint8_t*>(command.wifiMacAddr),
                     *command.matchKey, command.counter, command.playerUuid, command.teamUuid);
    } else if (command.command == MatchCmd::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(command.wifiMacAddr, command.matchId)) return;
        if (activeMatchState.matchIsReady || activeMatchState.pendingAckSeqId != 0) return;
        if (!player->getServerPlayerId().isZero() &&
            command.playerUuid == player->getServerPlayerId()) {
            // Cleared as on MATCH_ROLE_MISMATCH; nothing is sent, so the opponent's
            // side still goes ready.
            LOG_W(MATCH_MANAGER_TAG, "Rejecting MATCH_ID_ACK: self playerUuid");
            clearCurrentMatch();
            return;
        }
        matchChannel.cancel(activeMatchState.opponentMac.data());
        setOpponentIdentity(!activeMatchState.localIsHunter, command.playerUuid, command.teamUuid);
        auto* clock = SimpleTimer::getPlatformClock();
        LOG_D(MATCH_MANAGER_TAG, "TIMING matchAck-ready T=%lu", clock ? clock->milliseconds() : 0UL);
        activeMatchState.matchIsReady = true;
    } else if (command.command == MatchCmd::MATCH_ROLE_MISMATCH) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ROLE_MISMATCH command from opponent");
        if (!isFromActiveMatchOpponent(command.wifiMacAddr, command.matchId)) return;
        if (activeMatchState.matchIsReady || activeMatchState.pendingAckSeqId != 0) return;
        clearCurrentMatch();
        return;
    } else if (command.command == MatchCmd::DRAW_RESULT ||
               command.command == MatchCmd::NEVER_PRESSED) {
        LOG_I(MATCH_MANAGER_TAG, "Received DRAW_RESULT command from opponent");
        if (!isFromActiveMatchOpponent(command.wifiMacAddr, command.matchId)) return;
        // An opponent's frame naming this device's slot would overwrite the
        // local time and so pick the winner.
        if (command.isHunter == activeMatchState.localIsHunter) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting draw result: names the local draw slot");
            return;
        }
        // Not a reaction time, and as a signed effective time it could beat
        // any honest draw.
        if (command.playerDrawTime < 0) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting draw result: negative draw time");
            return;
        }
        const OpponentResult result{command.command, command.isHunter, command.playerDrawTime,
                                    command.boostMs};
        // Standalone countdowns start a frame delivery apart, so a press at the
        // very start of the opponent's window can land while this one still
        // counts down. The channel has acked it and will not resend it, so the
        // latest such result is held and applied as the window opens. Shootout
        // starts are fan-out retries and its results are taken as they come.
        if (!currentMatchIsShootout() && !activeMatchState.matchLocalStartTime.has_value()) {
            LOG_I(MATCH_MANAGER_TAG, "Holding draw result until the draw window opens");
            activeMatchState.earlyOpponentResult = result;
            return;
        }
        applyOpponentResult(result);
    } else {
        LOG_W(MATCH_MANAGER_TAG, "Received unexpected command in Match Manager: %d",
              command.command);
    }
}

void MatchManager::applyOpponentResult(const OpponentResult& result) {
    // No frame earns more than a full posse can give.
    activeMatchState.opponentBoostMs = std::min(result.boostMs, PosseManager::getMaxBoostMs());

    LOG_I(MATCH_MANAGER_TAG, "Opponent reaction time: %ld ms (boost %lu)", result.playerDrawTime,
          activeMatchState.opponentBoostMs);

    result.isHunter ? setHunterDrawTime(result.playerDrawTime)
                    : setBountyDrawTime(result.playerDrawTime);

    if (result.command == MatchCmd::NEVER_PRESSED) {
        activeMatchState.opponentNeverPressed = true;
    }

    setReceivedDrawResult();
}

void MatchManager::onApplicationFrame(SerialIdentifier jack, ApplicationFrameKind kind,
                                      const ApplicationPayload& payload) {
    if (kind != ApplicationFrameKind::MATCH_KEY) return;
    // A key belongs to the match it starts. This also drops this device's own
    // key echoed back through the contacts as the plug leaves.
    if (activeMatchState.match.has_value()) return;
    // The frame names no sender: only the peer on the opponent jack can have
    // written it. That link may still be connecting, since each side reaches
    // Connected on its own; pairing waits for it to be a Connected PDN.
    if (jack != opponentJack()) {
        LOG_W(MATCH_MANAGER_TAG, "Ignoring a match key from a jack other than the opponent jack");
        return;
    }
    const uint8_t* peerMac = rdc->getPeerMac(jack);
    if (peerMac == nullptr) {
        LOG_W(MATCH_MANAGER_TAG, "Ignoring a match key: no peer on the opponent jack");
        return;
    }
    wipePendingMatchKey();
    PendingMatchKey& held = pendingMatchKey.emplace(PendingMatchKey{});
    memcpy(held.fromMac.data(), peerMac, held.fromMac.size());
    memcpy(held.matchId.bytes, payload.data() + MATCH_KEY_FRAME_ID_OFFSET, UUIDBytes::SIZE);
    memcpy(held.key.data(), payload.data() + MATCH_KEY_FRAME_KEY_OFFSET, held.key.size());
    pairInitiationWithKey();
}

void MatchManager::onJackChange(SerialIdentifier jack, bool connected) {
    if (jack != opponentJack()) return;
    if (connected) {
        // A key that arrived while this link was still connecting can pair now.
        pairInitiationWithKey();
        return;
    }
    // A match already started keeps its key through a reseat; MatchApp
    // abandons it, and so wipes the key, if the cable stays out.
    wipePendingMatchKey();
    pendingInitiation.reset();
}

void MatchManager::onMatchInitiation(const uint8_t* fromMac, const MatchPacket& packet) {
    if (!activeMatchState.match.has_value()) {
        // Held first: the key frame can still be on the cable.
        PendingInitiation& held = pendingInitiation.emplace(PendingInitiation{});
        memcpy(held.fromMac.data(), fromMac, held.fromMac.size());
        held.packet = packet;
        if (pairInitiationWithKey()) return;
    }
    // No key yet: the gates and the role-mismatch reply need none, and nothing is primed.
    listenForMatchEvents(commandFromPacket(fromMac, packet));
}

bool MatchManager::pairInitiationWithKey() {
    if (!pendingMatchKey.has_value() || !pendingInitiation.has_value()) return false;
    if (pendingMatchKey->fromMac != pendingInitiation->fromMac) return false;
    if (!rdc->isConnectedPeer(opponentJack(), DeviceType::PDN, pendingMatchKey->fromMac.data())) {
        return false;
    }
    const MatchPacket& packet = pendingInitiation->packet;
    char matchIdText[IdGenerator::UUID_BUFFER_SIZE];
    IdGenerator::copyId(matchIdText, packet.matchId);
    UUIDBytes initiationId;
    if (!UUIDBytes::parse(matchIdText, initiationId) || initiationId != pendingMatchKey->matchId) {
        return false;
    }
    if (packet.counter == 0 || !matchPacketTagVerifies(pendingMatchKey->key, packet)) {
        // Only the initiation goes: the genuine one may still be on its way.
        LOG_W(MATCH_MANAGER_TAG, "Dropping SEND_MATCH_ID: its tag does not verify");
        pendingInitiation.reset();
        return true;
    }

    // The command points at local copies, so both slots can go before it runs.
    const std::array<uint8_t, 6> fromMac = pendingInitiation->fromMac;
    MatchKey key = pendingMatchKey->key;
    MatchCommand command = commandFromPacket(fromMac.data(), packet);
    command.counter = packet.counter;
    command.matchKey = &key;
    wipePendingMatchKey();
    pendingInitiation.reset();
    listenForMatchEvents(command);
    secureWipe(key.data(), key.size());
    return true;
}

bool MatchManager::admitMatchFrame(const uint8_t* fromMac, const MatchPacket& packet) {
    // Unsigned by design: a role mismatch comes from a device that holds no key
    // (forging one only denies a match), and an initiation is verified when it
    // pairs with the key off the cable.
    if (packet.command == MatchCmd::MATCH_ROLE_MISMATCH ||
        packet.command == MatchCmd::SEND_MATCH_ID) {
        return true;
    }
    if (isShootoutMatchId(packet.matchId)) return true;

    if (!activeMatchState.matchKeyIsHeld) {
        LOG_W(MATCH_MANAGER_TAG, "Dropping match frame %d: no match key held", packet.command);
        return false;
    }
    if (!isFromActiveMatchOpponent(fromMac, packet.matchId)) {
        LOG_W(MATCH_MANAGER_TAG, "Dropping match frame %d: not from the active match's opponent",
              packet.command);
        return false;
    }
    // A frame of this device's own, reflected back, carries a good tag.
    if ((packet.isHunter != 0) == activeMatchState.localIsHunter) {
        LOG_W(MATCH_MANAGER_TAG, "Dropping match frame %d: names this device's role",
              packet.command);
        return false;
    }
    if (!matchPacketTagVerifies(activeMatchState.matchKey, packet)) {
        LOG_W(MATCH_MANAGER_TAG, "Dropping match frame %d: tag does not verify", packet.command);
        return false;
    }
    if (packet.counter <= activeMatchState.rxCounter) {
        LOG_W(MATCH_MANAGER_TAG, "Dropping match frame %d: counter %u is not above %u",
              packet.command, static_cast<unsigned>(packet.counter),
              static_cast<unsigned>(activeMatchState.rxCounter));
        return false;
    }
    activeMatchState.rxCounter = packet.counter;
    return true;
}

void MatchManager::sendNeverPressed(unsigned long pityTime) {
    if (!activeMatchState.match.has_value()) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot send NEVER_PRESSED - no active match");
        return;
    }

    activeMatchState.localIsHunter ? setHunterDrawTime(pityTime) : setBountyDrawTime(pityTime);
    setNeverPressed();
    // Mirror what gets uploaded: every match contributes a draw time to the server,
    // so on-device "average reaction" should include pity times too.
    player->addReactionTime(pityTime);

    sendCommand(activeMatchState.opponentMac.data(), MatchCmd::NEVER_PRESSED,
                activeMatchState.match->getMatchId(), pityTime, 0, activeMatchState.localIsHunter);
}

bool MatchManager::isMatchReady() const { return activeMatchState.matchIsReady; }