#include "game/match-manager.hpp"
#include <ArduinoJson.h>
#include "device/drivers/logger.hpp"
#include "game/shootout-manager.hpp"
#include "id-generator.hpp"
#include "wireless/mac-functions.hpp"
#include "wireless/wireless-transport.hpp"
#include <optional>

static constexpr const char* PREF_COUNT_KEY = "count";
static constexpr const char* PREF_MATCH_KEY  = "match_";
static constexpr uint8_t     MAX_MATCHES      = 255;

static const char* const MATCH_MANAGER_TAG = "MATCH_MANAGER";

MatchManager::MatchManager()
    : player(nullptr)
    , storage(nullptr) {
}

MatchManager::~MatchManager() {
    // The channel outlives this manager (the transport owns it); detach the
    // receive handler so a late packet can't dispatch into freed memory.
    if (channel_) {
        channel_->onReceive(nullptr);
    }
    if (storage) {
        storage->end();
    }
}

void MatchManager::setBoostProvider(std::function<unsigned long()> provider) {
    boostProvider_ = provider;
}

void MatchManager::setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc) {
    rdc_ = rdc;
}

void MatchManager::setShootoutManager(ShootoutManager* shootoutManager) {
    shootoutManager_ = shootoutManager;
}

ShootoutManager* MatchManager::getShootoutManager() const {
    return shootoutManager_;
}

void MatchManager::clearCurrentMatch() {
    if (activeDuelState.match) {
        LOG_I(MATCH_MANAGER_TAG, "Clearing current match");
        // Cancel (never abandon) any in-flight reliable send to the opponent
        // before the MAC is wiped: cancel fires no abandon callback, so a
        // stale abandon can't void a future match primed against the same peer.
        if (channel_) {
            channel_->cancel(activeDuelState.opponentMac.data());
        }
        activeDuelState = {};
    }
}

void MatchManager::voidCurrentMatch() {
    if (!activeDuelState.match) return;
    LOG_W(MATCH_MANAGER_TAG, "Voiding match %s",
          activeDuelState.match->getMatchId());
    activeDuelState.match->setVoided(true);
}

void MatchManager::onReliableSendAbandoned(const uint8_t* target) {
    // Ignore an abandon for anyone but the current opponent: a stale abandon for
    // a prior peer must not void a match freshly primed against a different one.
    if (!activeDuelState.match) return;
    if (target == nullptr ||
        memcmp(target, activeDuelState.opponentMac.data(), 6) != 0) {
        return;
    }

    // Setup-phase failure: an unacked SEND_MATCH_ID means the initiator never
    // reached the peer, so the match never became ready and no result is
    // committed. Drop the half-formed match so Idle re-initiates against the
    // peer.
    // A ready match (responder already in countdown) is left alone: opponent
    // loss there is handled by the result-phase abandon below.
    if (!activeDuelState.myResult.has_value()) {
        if (!activeDuelState.matchIsReady) clearCurrentMatch();
        return;
    }

    // Result-phase failure: we committed a result locally but it never reached
    // the opponent. Void only if we pressed; a non-presser's recorded loss
    // stands rather than being overwritten with a void.
    if (!getHasPressedButton()) return;
    voidCurrentMatch();
}

void MatchManager::primeMatch(const char* matchId, const uint8_t* opponentMac, bool localIsHunter) {
    activeDuelState.localIsHunter = localIsHunter;
    activeDuelState.match.emplace(matchId, player->getUserID().c_str(), localIsHunter);
    memcpy(activeDuelState.opponentMac.data(), opponentMac, 6);
}

void MatchManager::initializeMatch(uint8_t* opponentMac) {
    if (activeDuelState.match.has_value()) {
        return;
    }

    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    memcpy(matchId, IdGenerator::getInstance().generateId(), IdGenerator::UUID_BUFFER_SIZE);
    // Normal duel: the per-match draw-slot is the player's own global role.
    primeMatch(matchId, opponentMac, player->isHunter());
    sendMatchId();
}

void MatchManager::sendMatchId() {
    sendReliable(activeDuelState.opponentMac.data(),
                 makeOwnPacket(QDCommand::SEND_MATCH_ID,
                               activeDuelState.match->getMatchId(), 0,
                               localIsHunterForMatch()));
}

void MatchManager::sendReliable(const uint8_t* macAddress, const QuickdrawPacket& packet) {
    if (channel_ == nullptr) {
        return;
    }
    uint8_t seqId = channel_->sendReliable(macAddress, packet);
    LOG_I(MATCH_MANAGER_TAG, "Sending reliable command %i to %s seqId=%u",
          packet.command, MacToString(macAddress), (unsigned)seqId);
}

void MatchManager::initializeShootoutMatch(const char* matchId, uint8_t* opponentMac,
                                           bool localIsHunter) {
    if (activeDuelState.match.has_value()) return;

    primeMatch(matchId, opponentMac, localIsHunter);
    activeDuelState.isShootout = true;
    activeDuelState.matchIsReady = true;
}

void MatchManager::receiveMatch(const char* matchId, const char* opponentId, bool opponentIsHunter, uint8_t* opponentMac) {
    if (activeDuelState.match.has_value()) {
        return;
    }

    // A received (non-shootout) match uses our global role as the draw-slot.
    activeDuelState.localIsHunter = player->isHunter();
    activeDuelState.match.emplace(matchId, player->getUserID().c_str(), activeDuelState.localIsHunter);
    if (opponentIsHunter) {
        activeDuelState.match->setHunterId(opponentId);
    } else {
        activeDuelState.match->setBountyId(opponentId);
    }
    memcpy(activeDuelState.opponentMac.data(), opponentMac, 6);

    activeDuelState.matchIsReady = true;

    sendMatchAck();
}


void MatchManager::setDuelLocalStartTime(unsigned long local_start_time_ms) {
    activeDuelState.duelLocalStartTime = local_start_time_ms;
}

unsigned long MatchManager::getDuelLocalStartTime() {
    return activeDuelState.duelLocalStartTime;
}

bool MatchManager::matchResultsAreIn() {
    if (!activeDuelState.match) {
        return false;
    }

    if (activeDuelState.match->isVoided()) return true;

    const auto& mine = activeDuelState.myResult;
    if (!mine) return false;
    // A local timeout resolves the duel without waiting on the opponent (the
    // deadlock escape for a dropped NEVER_PRESSED).
    return mine->pressed ? activeDuelState.theirResult.has_value() : true;
}

bool MatchManager::didWin() {
    if (!activeDuelState.match) {
        return false;
    }

    if (activeDuelState.match->isVoided()) return false;

    // Opponent timed out -> I win. Checked before my own timeout to preserve the
    // both-timed-out tiebreak (server match_id dedup reconciles the rare
    // symmetric double-claim).
    if (activeDuelState.theirResult && !activeDuelState.theirResult->pressed) {
        return true;
    }
    if (activeDuelState.myResult && !activeDuelState.myResult->pressed) {
        return false;
    }

    return localIsHunterForMatch() ?
    activeDuelState.match->getHunterDrawTime() < activeDuelState.match->getBountyDrawTime() :
    activeDuelState.match->getBountyDrawTime() < activeDuelState.match->getHunterDrawTime();
}

bool MatchManager::finalizeMatch() {
    if (!activeDuelState.match) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot finalize match - no active match or ID mismatch\n");
        return false;
    }

    if (!activeDuelState.match->isVoided()) {
        // Snapshot times for the result screen (before clearCurrentMatch wipes the match).
        lastMatchDisplay_.myTimeMs = localIsHunterForMatch()
            ? activeDuelState.match->getHunterDrawTime()
            : activeDuelState.match->getBountyDrawTime();
        lastMatchDisplay_.opponentTimeMs = localIsHunterForMatch()
            ? activeDuelState.match->getBountyDrawTime()
            : activeDuelState.match->getHunterDrawTime();
        lastMatchDisplay_.hasData = true;
    }

    std::string match_id = activeDuelState.match->getMatchId();

    // Shootout matches are local-ephemeral: no save, no upload.
    if (currentMatchIsShootout()) {
        clearCurrentMatch();
        return true;
    }

    if (appendMatchToStorage(&*activeDuelState.match)) {
        updateStoredMatchCount(getStoredMatchCount() + 1);
        const char* tag = activeDuelState.match->isVoided() ? "voided" : "completed";
        LOG_I(MATCH_MANAGER_TAG, "Persisted %s match %s", tag, match_id.c_str());
        clearCurrentMatch();
        return true;
    }

    LOG_E("PDN", "Failed to finalize match %s\n", match_id.c_str());
    return false;
}

bool MatchManager::currentMatchIsShootout() const {
    return activeDuelState.match.has_value() && activeDuelState.isShootout;
}

bool MatchManager::getHasReceivedDrawResult() {
    return activeDuelState.theirResult.has_value();
}

bool MatchManager::getHasPressedButton() {
    return activeDuelState.myResult.has_value() && activeDuelState.myResult->pressed;
}

parameterizedCallbackFunction MatchManager::getDuelButtonPush() {
    return duelButtonPush;
}

void MatchManager::setReceivedButtonPush() {
    activeDuelState.myResult = ActiveDuelState::SideResult{true};
}

bool MatchManager::setHunterDrawTime(unsigned long hunter_time_ms) {
    if (!activeDuelState.match) {
        return false;
    }
    activeDuelState.match->setHunterDrawTime(hunter_time_ms);
    return true;
}

bool MatchManager::setBountyDrawTime(unsigned long bounty_time_ms) {
    if (!activeDuelState.match) {
        return false;
    }
    activeDuelState.match->setBountyDrawTime(bounty_time_ms);
    return true;
}

std::string MatchManager::toJson() {
    JsonDocument doc;  // Adjust size based on max matches
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

void MatchManager::clearStorage() {
    storage->clear();
    updateStoredMatchCount(0);
    LOG_I("PDN", "Cleared match storage\n");
}

size_t MatchManager::getStoredMatchCount() {
    return storage->readUChar(PREF_COUNT_KEY, 0);
}

bool MatchManager::appendMatchToStorage(const Match* match) {
    if (!match) return false;

    uint8_t count = getStoredMatchCount();
    if (count >= MAX_MATCHES) {
        LOG_E("PDN", "Cannot save match - storage full\n");
        return false;
    }

    std::string matchJson = match->toJson();

    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, count);

    LOG_W(MATCH_MANAGER_TAG, "Attempting to save match to storage at key %s (JSON length: %d bytes)", 
            key, matchJson.length());
    LOG_W(MATCH_MANAGER_TAG, "Match JSON: %s", matchJson.c_str());

    if (storage->writeUChar("test_key", 123) != 1) {
        LOG_E(MATCH_MANAGER_TAG, "NVS Preference test write failed! Potential hardware or NVS issue");
    } else {
        uint8_t test_val = storage->readUChar("test_key", 0);
        LOG_W(MATCH_MANAGER_TAG, "NVS test write/read successful: wrote 123, read %d", test_val);
    }

    if (storage->write(key, matchJson) != matchJson.length()) {
        LOG_E(MATCH_MANAGER_TAG, "Failed to save match to storage - key: %s, length: %d", 
                key, matchJson.length());

        return false;
    }

    LOG_W(MATCH_MANAGER_TAG, "Successfully saved match to storage at index %d", count);
    return true;
}

void MatchManager::updateStoredMatchCount(uint8_t count) {
    if (storage->writeUChar(PREF_COUNT_KEY, count) != 1) {
        LOG_E("PDN", "Failed to update match count\n");
    } else {
        LOG_I("PDN", "Updated stored match count to %d\n", count);
    }
}

Match* MatchManager::readMatchFromStorage(uint8_t index) {
    if (index >= getStoredMatchCount()) {
        return nullptr;
    }

    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_MATCH_KEY, index);

    std::string matchJson = storage->read(key, "");
    if (matchJson.empty()) {
        return nullptr;
    }

    Match* match = new Match();
    match->fromJson(matchJson);
    return match;
}

parameterizedCallbackFunction MatchManager::getButtonMasher() {
    return buttonMasher;
}

void MatchManager::initialize(Player* player, StorageInterface* storage, WirelessTransport* transport) {
    this->player = player;
    this->storage = storage;

    if (transport != nullptr) {
        channel_ = transport->channel<QuickdrawPacket>(
            PktType::kQuickdrawCommand,
            [this](uint8_t seqId, const uint8_t* target) {
                LOG_W(MATCH_MANAGER_TAG,
                      "kQuickdrawCommand abandoned: target=%s seqId=%u",
                      MacToString(target), (unsigned)seqId);
                onReliableSendAbandoned(target);
            });
        // deliverIncoming acks and dedups before this fires; one path shared
        // with every reliable channel.
        channel_->onReceive(
            [this](const uint8_t* fromMac, const QuickdrawPacket& packet) {
                listenForMatchEvents(fromMac, packet);
            });
    }

    duelButtonPush = [](void *ctx) {
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
        ActiveDuelState* activeDuelState = &matchManager->activeDuelState;
        Player *player = matchManager->player;

        if(activeDuelState->myResult.has_value()) {
            LOG_I(MATCH_MANAGER_TAG, "Duel outcome already resolved - skipping press");
            return;
        }

        if(!matchManager->getCurrentMatch().has_value()) {
            LOG_E(MATCH_MANAGER_TAG, "No current match - skipping");
            return;
        }

        unsigned long reactionTimeMs = now - matchManager->getDuelLocalStartTime();

        if(activeDuelState->buttonMasherCount > 0) {
            reactionTimeMs = reactionTimeMs + activeDuelState->calculateButtonMasherPenalty();
        }

        // Chain boost never applies to a shootout match: a ring has no
        // supporters, but a chain closed into a ring without unplugging leaves
        // stale confirms on the ex-champion, and the provider would hand its
        // duelist a head start.
        const bool chainBoostApplies = !matchManager->currentMatchIsShootout();
        unsigned long boost = (chainBoostApplies && matchManager->boostProvider_)
                                  ? matchManager->boostProvider_()
                                  : 0;
        unsigned long boostedTimeMs = (boost >= reactionTimeMs) ? 0 : (reactionTimeMs - boost);

            // Snapshot boost at press time so the result screen can display it
        // even if supporters unplug before the match finalizes.
        matchManager->lastMatchDisplay_.boostMs = boost;

        LOG_I(MATCH_MANAGER_TAG, "Button pressed! Reaction time: %lu ms (boost %lu) for %s",
                boostedTimeMs, boost, matchManager->localIsHunterForMatch() ? "Hunter" : "Bounty");

        matchManager->localIsHunterForMatch() ?
        matchManager->setHunterDrawTime(boostedTimeMs)
        : matchManager->setBountyDrawTime(boostedTimeMs);

        // Player stats record the raw (unboosted) reaction time — boost is a
        // duel-time advantage, not an achievement the player actually made.
        // Shootout matches are venue-local (never uploaded), so they stay out
        // of the lifetime reaction-time average too.
        if (!matchManager->currentMatchIsShootout()) {
            player->addReactionTime(reactionTimeMs);
        }

        LOG_I(MATCH_MANAGER_TAG, "Stored reaction time in MatchManager");

        LOG_I(MATCH_MANAGER_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s",
                MacToString(activeDuelState->opponentMac.data()));

        // Send the BOOSTED time in DRAW_RESULT so both sides agree on who won.
        matchManager->sendReliable(
            activeDuelState->opponentMac.data(),
            matchManager->makeOwnPacket(QDCommand::DRAW_RESULT,
                                        matchManager->getCurrentMatch()->getMatchId(),
                                        boostedTimeMs,
                                        matchManager->localIsHunterForMatch()));

        matchManager->setReceivedButtonPush();

        LOG_I(MATCH_MANAGER_TAG, "Reaction time: %lu ms", reactionTimeMs);
    };

    buttonMasher = [](void *ctx) {
        MatchManager* matchManager = static_cast<MatchManager*>(ctx);
        matchManager->activeDuelState.buttonMasherCount++;
        LOG_I(MATCH_MANAGER_TAG, "Button masher count: %d", matchManager->activeDuelState.buttonMasherCount);
    };
}

void MatchManager::sendMatchAck() {
    sendReliable(activeDuelState.opponentMac.data(),
                 makeOwnPacket(QDCommand::MATCH_ID_ACK,
                               activeDuelState.match->getMatchId(), 0,
                               localIsHunterForMatch()));
}

// Send the mismatch notification using the INCOMING packet's identifiers.
// This path fires BEFORE activeDuelState.match is populated (receiveMatch has
// not run), so we cannot read matchId/opponentMac from activeDuelState — the
// optional is empty and opponentMac is zero/stale. Echo the sender's data
// instead so the rejection actually reaches them. Sent reliably so the
// initiator (whose SEND_MATCH_ID was already transport-acked, hence won't
// retry) is guaranteed to learn the pairing is invalid and clear its match.
void MatchManager::sendMatchRoleMismatch(const uint8_t* fromMac,
                                         const QuickdrawPacket& incoming) {
    sendReliable(fromMac, makeOwnPacket(QDCommand::MATCH_ROLE_MISMATCH,
                                        incoming.matchId, 0, player->isHunter()));
}

bool MatchManager::isFromActiveMatchOpponent(const uint8_t* fromMac,
                                             const QuickdrawPacket& packet) const {
    if (!activeDuelState.match.has_value()) return false;
    if (memcmp(fromMac, activeDuelState.opponentMac.data(), 6) != 0) return false;
    if (strcmp(packet.matchId, activeDuelState.match->getMatchId()) != 0) return false;
    return true;
}

void MatchManager::listenForMatchEvents(const uint8_t* fromMac,
                                        const QuickdrawPacket& packet) {
    LOG_I(MATCH_MANAGER_TAG, "Received command: %d", packet.command);

    if(packet.command == QDCommand::SEND_MATCH_ID) {
        // Gate on RDC peering — no active match exists yet, so the sender
        // must be authenticated via the cable-established direct peer.
        LOG_I(MATCH_MANAGER_TAG, "Received SEND_MATCH_ID command from opponent");
        if (!rdc_ || !rdc_->isDirectPeer(fromMac)) {
            LOG_W(MATCH_MANAGER_TAG, "Rejecting SEND_MATCH_ID: sender not an RDC direct peer");
            return;
        }
        bool shootoutActive = shootoutManager_ && shootoutManager_->active();
        if(!shootoutActive && player->isHunter() == packet.isHunter) {
            // Same role on both sides — not a valid hunter/bounty pairing.
            // Shootout brackets intentionally allow same-role duels.
            sendMatchRoleMismatch(fromMac, packet);
            return;
        } else {
            receiveMatch(packet.matchId, packet.playerId, packet.isHunter, const_cast<uint8_t*>(fromMac));
        }
    } else if(packet.command == QDCommand::MATCH_ID_ACK) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ID_ACK command from opponent");
        if (!isFromActiveMatchOpponent(fromMac, packet)) return;
        if(localIsHunterForMatch()) { activeDuelState.match->setBountyId(packet.playerId); }
        else { activeDuelState.match->setHunterId(packet.playerId); }
        activeDuelState.matchIsReady = true;
    } else if(packet.command == QDCommand::MATCH_ROLE_MISMATCH) {
        LOG_I(MATCH_MANAGER_TAG, "Received MATCH_ROLE_MISMATCH command from opponent");
        if (!isFromActiveMatchOpponent(fromMac, packet)) return;
        clearCurrentMatch();
        return;
    } else if(packet.command == QDCommand::DRAW_RESULT || packet.command == QDCommand::NEVER_PRESSED) {
        LOG_I(MATCH_MANAGER_TAG, "Received DRAW_RESULT command from opponent");
        if (!isFromActiveMatchOpponent(fromMac, packet)) return;

        long opponentTime = packet.playerDrawTime;

        LOG_I(MATCH_MANAGER_TAG, "Opponent reaction time: %ld ms", opponentTime);

        // First terminal packet from the opponent resolves their side; ignore a
        // later one (a retransmit, or a NEVER_PRESSED racing a DRAW_RESULT). The
        // draw-time write is part of that resolution: a second packet must not
        // clobber the recorded time the winner comparison reads.
        if (!activeDuelState.theirResult) {
            packet.isHunter ?
            setHunterDrawTime(opponentTime)
            : setBountyDrawTime(opponentTime);
            activeDuelState.theirResult = ActiveDuelState::SideResult{
                packet.command != QDCommand::NEVER_PRESSED};
        }
    } else {
        LOG_W(MATCH_MANAGER_TAG, "Received unexpected command in Match Manager: %d", packet.command);
    }
}

void MatchManager::sendNeverPressed(unsigned long pityTime) {
    if (!activeDuelState.match.has_value()) {
        LOG_E(MATCH_MANAGER_TAG, "Cannot send NEVER_PRESSED - no active match");
        return;
    }
    // First writer wins: a press landing the same tick the grace expired already
    // resolved my side (execDrivers runs the button handler before the state
    // loop). Don't overwrite it with a timeout or broadcast a spurious
    // NEVER_PRESSED.
    if (activeDuelState.myResult.has_value()) return;

    localIsHunterForMatch() ? setHunterDrawTime(pityTime) : setBountyDrawTime(pityTime);
    activeDuelState.myResult = ActiveDuelState::SideResult{false};
    // Mirror what gets uploaded: every duel contributes a draw time to the server,
    // so on-device "average reaction" should include pity times too. Shootout
    // matches upload nothing, so they contribute nothing here either.
    if (!currentMatchIsShootout()) {
        player->addReactionTime(pityTime);
    }

    sendReliable(activeDuelState.opponentMac.data(),
                 makeOwnPacket(QDCommand::NEVER_PRESSED,
                               activeDuelState.match->getMatchId(), pityTime,
                               localIsHunterForMatch()));
}

bool MatchManager::isMatchReady() {
    return activeDuelState.matchIsReady;
}