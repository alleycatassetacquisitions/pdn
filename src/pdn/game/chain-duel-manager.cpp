#include "game/chain-duel-manager.hpp"
#include "device/drivers/logger.hpp"

#define TAG "CDM"

ChainDuelManager::ChainDuelManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc) {
    this->player_ = player;
    this->wirelessManager_ = wirelessManager;
    this->rdc_ = rdc;
}

SerialIdentifier ChainDuelManager::opponentJack() const {
    return player_->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
}

SerialIdentifier ChainDuelManager::supporterJack() const {
    return player_->isHunter() ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
}

std::optional<bool> ChainDuelManager::peerIsHunter(SerialIdentifier port) const {
    return peerRoleByPort_[port == SerialIdentifier::INPUT_JACK ? 0 : 1];
}

bool ChainDuelManager::isLoop() const {
    PortState oState = rdc_->getPortState(opponentJack());
    PortState sState = rdc_->getPortState(supporterJack());
    for (const auto& oPeer : oState.peerMacAddresses) {
        for (const auto& sPeer : sState.peerMacAddresses) {
            if (memcmp(oPeer.data(), sPeer.data(), 6) == 0) return true;
        }
    }
    return false;
}

bool ChainDuelManager::isSupporter() const {
    auto opponentRole = peerIsHunter(opponentJack());
    if (!opponentRole.has_value()) return false;
    if (*opponentRole != player_->isHunter()) return false;
    return !isLoop();
}

bool ChainDuelManager::isChampion() const {
    // Default-champion: head of own-role chain. Demoted to supporter only
    // when a same-role peer sits on our opponent jack. Rings resolved by
    // isLoop() check (no champion inside a ring).
    return !isSupporter() && !isLoop();
}

bool ChainDuelManager::canInitiateMatch() const {
    if (!player_->isHunter()) return false;
    auto opponentRole = peerIsHunter(opponentJack());
    if (!opponentRole.has_value()) return false;
    return *opponentRole != player_->isHunter();
}

std::vector<std::array<uint8_t, 6>> ChainDuelManager::getSupporterChainPeers() const {
    if (isLoop()) return {};
    PortState state = rdc_->getPortState(supporterJack());
    return state.peerMacAddresses;
}

bool ChainDuelManager::isKnownGameEventSender(const uint8_t* fromMac) const {
    PortState oState = rdc_->getPortState(opponentJack());
    for (const auto& peer : oState.peerMacAddresses) {
        if (memcmp(peer.data(), fromMac, 6) == 0) return true;
    }
    return false;
}

void ChainDuelManager::sendGameEventToSupporters(ChainGameEventType eventType) {
    if (!isChampion()) return;

    if (eventType == ChainGameEventType::COUNTDOWN) {
        clearSupporterConfirms();
    }

    // WIN/LOSS are state-terminal for the supporter UI and must arrive or
    // the supporter display sticks on a stale screen until the next chain
    // event. They get seqIds and retry tracking.
    //
    // COUNTDOWN/DRAW are time-sensitive transient events. A late-arriving
    // retry of COUNTDOWN would falsely re-arm a supporter whose duel has
    // already resolved; a late DRAW would disarm a supporter who just
    // entered a new COUNTDOWN. Keep them fire-and-forget: seqId=0.
    bool wantsAck = (eventType == ChainGameEventType::WIN ||
                     eventType == ChainGameEventType::LOSS);

    auto peers = getSupporterChainPeers();
    for (const auto& peerMac : peers) {
        ChainGameEventPayload payload{};
        payload.event_type = static_cast<uint8_t>(eventType);
        payload.seqId = 0;

        if (wantsAck) {
            uint8_t seqId = nextGameEventSeqId_++;
            if (nextGameEventSeqId_ == 0) nextGameEventSeqId_ = 1;
            payload.seqId = seqId;

            // One pending per supporter; newest supersedes any prior.
            for (auto it = pendingGameEvents_.begin(); it != pendingGameEvents_.end(); ++it) {
                if (memcmp(it->targetMac.data(), peerMac.data(), 6) == 0) {
                    pendingGameEvents_.erase(it);
                    break;
                }
            }
            PendingGameEvent pending;
            pending.targetMac = peerMac;
            pending.seqId = seqId;
            pending.eventType = static_cast<uint8_t>(eventType);
            pending.retries = 0;
            pending.timer.setTimer(kAckTimeoutMs);
            pendingGameEvents_.push_back(pending);
            retryStats_.sends++;
        }

        wirelessManager_->sendEspNowData(
            peerMac.data(),
            PktType::kChainGameEvent,
            reinterpret_cast<const uint8_t*>(&payload),
            sizeof(payload));
    }
}

void ChainDuelManager::sendGameEventAck(const uint8_t* toMac, uint8_t seqId) {
    if (toMac == nullptr || seqId == 0) return;
    ChainGameEventAckPayload ack{};
    ack.seqId = seqId;
    wirelessManager_->sendEspNowData(
        toMac, PktType::kChainGameEventAck,
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

void ChainDuelManager::onChainGameEventAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (fromMac == nullptr || seqId == 0) return;
    for (auto it = pendingGameEvents_.begin(); it != pendingGameEvents_.end(); ++it) {
        if (it->seqId == seqId && memcmp(it->targetMac.data(), fromMac, 6) == 0) {
            retryStats_.ackLatencyMsSum += it->timer.getElapsedTime();
            retryStats_.ackCount++;
            pendingGameEvents_.erase(it);
            return;
        }
    }
}

void ChainDuelManager::sendConfirm() {
    if (!championMac_.has_value()) return;

    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return;

    ChainConfirmPayload payload{};
    memcpy(payload.originatorMac, selfMac, 6);
    payload.seqId = nextConfirmSeqId_++;
    if (nextConfirmSeqId_ == 0) nextConfirmSeqId_ = 1;

    wirelessManager_->sendEspNowData(
        championMac_->data(),
        PktType::kChainConfirm,
        reinterpret_cast<const uint8_t*>(&payload),
        sizeof(payload));
}

void ChainDuelManager::onConfirmReceived(
    const uint8_t* fromMac,
    const uint8_t* originatorMac,
    uint8_t seqId) {
    (void)fromMac; (void)seqId;
    if (!isChampion()) return;

    auto peers = getSupporterChainPeers();
    bool isMember = false;
    for (const auto& peer : peers) {
        if (memcmp(peer.data(), originatorMac, 6) == 0) { isMember = true; break; }
    }
    if (!isMember) return;

    for (const auto& existing : confirmedSupporters_) {
        if (memcmp(existing.data(), originatorMac, 6) == 0) return;
    }
    std::array<uint8_t, 6> macArr;
    memcpy(macArr.data(), originatorMac, 6);
    confirmedSupporters_.push_back(macArr);
    boostMs_ = confirmedSupporters_.size() * BOOST_PER_SUPPORTER_MS;
}

void ChainDuelManager::onChainStateChanged() {
    PortState sState = rdc_->getPortState(supporterJack());
    size_t count = sState.peerMacAddresses.size();
    if (lastSupporterChainCount_ > 0 && count == 0) {
        clearSupporterConfirms();
    }
    lastSupporterChainCount_ = count;

    // Drop cached peer roles for any port that no longer has a direct peer.
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        if (rdc_->getPeerMac(port) == nullptr) {
            peerRoleByPort_[port == SerialIdentifier::INPUT_JACK ? 0 : 1].reset();
        }
    }

    // Re-evaluate championMac_. If I'm now champion, self-assign.
    if (isChampion()) {
        const uint8_t* selfMac = wirelessManager_->getMacAddress();
        if (selfMac != nullptr) {
            std::array<uint8_t, 6> selfArr;
            memcpy(selfArr.data(), selfMac, 6);
            if (!championMac_.has_value() || *championMac_ != selfArr) {
                championMac_ = selfArr;
                broadcastRoleAndChampion();
                // Track that we just announced to our current supporter-jack peer.
                const uint8_t* supporterPeer = rdc_->getPeerMac(supporterJack());
                if (supporterPeer != nullptr) {
                    std::array<uint8_t, 6> cur;
                    memcpy(cur.data(), supporterPeer, 6);
                    lastAnnouncedSupporterJackMac_ = cur;
                }
                // Announce role to opponent-jack peer if new.
                const uint8_t* opponentPeer = rdc_->getPeerMac(opponentJack());
                if (opponentPeer != nullptr) {
                    std::array<uint8_t, 6> cur;
                    memcpy(cur.data(), opponentPeer, 6);
                    if (!lastAnnouncedOpponentJackMac_.has_value() || *lastAnnouncedOpponentJackMac_ != cur) {
                        lastAnnouncedOpponentJackMac_ = cur;
                        sendRoleToOpponentJack();
                    }
                }
                return;
            }
        }
    }

    // If we've become a supporter but still hold our own MAC as champion,
    // invalidate it — the real champion's announce will repopulate via
    // onRoleAnnounceReceived.
    if (isSupporter() && championMac_.has_value()) {
        const uint8_t* selfMac = wirelessManager_->getMacAddress();
        if (selfMac != nullptr && memcmp(championMac_->data(), selfMac, 6) == 0) {
            championMac_.reset();
        }
    }

    // If a new supporter-jack direct peer has appeared since our last announce,
    // broadcast the current championMac to it. Guard against re-firing on every
    // chain-state event (which would cause packet storms and destabilize the
    // serial heartbeat timing).
    const uint8_t* supporterPeer = rdc_->getPeerMac(supporterJack());
    if (championMac_.has_value() && supporterPeer != nullptr) {
        std::array<uint8_t, 6> cur;
        memcpy(cur.data(), supporterPeer, 6);
        if (!lastAnnouncedSupporterJackMac_.has_value() || *lastAnnouncedSupporterJackMac_ != cur) {
            lastAnnouncedSupporterJackMac_ = cur;
            broadcastRoleAndChampion();
        }
    } else if (supporterPeer == nullptr) {
        lastAnnouncedSupporterJackMac_.reset();
    }

    // Announce our role to the opponent-jack peer if it has changed.
    const uint8_t* opponentPeer = rdc_->getPeerMac(opponentJack());
    if (opponentPeer != nullptr) {
        std::array<uint8_t, 6> cur;
        memcpy(cur.data(), opponentPeer, 6);
        if (!lastAnnouncedOpponentJackMac_.has_value() || *lastAnnouncedOpponentJackMac_ != cur) {
            lastAnnouncedOpponentJackMac_ = cur;
            sendRoleToOpponentJack();
        }
    } else {
        lastAnnouncedOpponentJackMac_.reset();
    }
}

void ChainDuelManager::setPeerRole(SerialIdentifier port, bool isHunter) {
    peerRoleByPort_[port == SerialIdentifier::INPUT_JACK ? 0 : 1] = isHunter;
}

unsigned long ChainDuelManager::getBoostMs() const {
    return boostMs_;
}

size_t ChainDuelManager::getConfirmedSupporterCount() const {
    return confirmedSupporters_.size();
}

void ChainDuelManager::clearSupporterConfirms() {
    confirmedSupporters_.clear();
    boostMs_ = 0;
}

const uint8_t* ChainDuelManager::getChampionMac() const {
    return championMac_.has_value() ? championMac_->data() : nullptr;
}

void ChainDuelManager::onRoleAnnounceReceived(
    const uint8_t* fromMac,
    uint8_t role,
    const uint8_t* championMac,
    uint8_t seqId) {
    // 1. Update peer role based on which jack fromMac is on.
    //    Also remember whether this announce came from our opponent-jack
    //    (parent) direction — only opponent-jack announces authoritatively
    //    update our championMac_ cache.
    bool fromOpponentJack = false;
    bool fromKnownDirectPeer = false;
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const uint8_t* directMac = rdc_->getPeerMac(port);
        if (directMac != nullptr && memcmp(directMac, fromMac, 6) == 0) {
            setPeerRole(port, role == 1);
            fromKnownDirectPeer = true;
            if (port == opponentJack()) {
                fromOpponentJack = true;
            }
            break;
        }
    }

    // 2. Only ACK known direct peers. Strangers in radio range must not be
    //    able to probe device liveness by eliciting an ACK.
    if (!fromKnownDirectPeer) return;

    RoleAnnounceAckPayload ack{};
    ack.seqId = seqId;
    wirelessManager_->sendEspNowData(
        fromMac, PktType::kRoleAnnounceAck,
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

    // 3 & 4. Only same-role opponent-jack announces authoritatively update
    // championMac_. Opposite-role senders are dueling opponents, not chain
    // parents — their championMac is irrelevant.
    if (!fromOpponentJack) return;
    if (role != (player_->isHunter() ? 1u : 0u)) return;

    // 3. Register champion as ESP-NOW peer (only if it's not our own MAC).
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    bool championIsSelf = (selfMac != nullptr &&
                           memcmp(selfMac, championMac, 6) == 0);
    if (!championIsSelf) {
        rdc_->registerPeer(championMac);
    }

    // 4. Update championMac_ and cascade if changed. On change, release the
    // ESP-NOW peer slot held by the OLD champion MAC unless it's still
    // reachable via one of our jacks (then its registration is owned by the
    // chain-peer bookkeeping and will be cleaned up when that list changes).
    std::array<uint8_t, 6> newMac;
    memcpy(newMac.data(), championMac, 6);
    bool changed = !championMac_.has_value() || *championMac_ != newMac;
    if (changed && championMac_.has_value()) {
        std::array<uint8_t, 6> oldMac = *championMac_;
        bool oldIsSelf = (selfMac != nullptr &&
                          memcmp(selfMac, oldMac.data(), 6) == 0);
        if (!oldIsSelf) {
            bool oldStillInChain = false;
            for (SerialIdentifier p : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
                PortState ps = rdc_->getPortState(p);
                for (const auto& m : ps.peerMacAddresses) {
                    if (memcmp(m.data(), oldMac.data(), 6) == 0) {
                        oldStillInChain = true;
                        break;
                    }
                }
                if (oldStillInChain) break;
            }
            if (!oldStillInChain) {
                rdc_->unregisterPeer(oldMac.data());
            }
        }
    }
    championMac_ = newMac;
    if (changed) {
        broadcastRoleAndChampion();
    }
}

void ChainDuelManager::broadcastRoleAndChampion() {
    if (!championMac_.has_value()) return;

    const uint8_t* supporterPeer = rdc_->getPeerMac(supporterJack());
    if (supporterPeer == nullptr) return;

    uint8_t seqId = nextRoleAnnounceSeqId_++;
    if (nextRoleAnnounceSeqId_ == 0) nextRoleAnnounceSeqId_ = 1;

    RoleAnnouncePayload payload{};
    payload.role = player_->isHunter() ? 1 : 0;
    memcpy(payload.championMac, championMac_->data(), 6);
    payload.seqId = seqId;

    memcpy(pending_.targetMac.data(), supporterPeer, 6);
    pending_.championMac = *championMac_;
    pending_.role = payload.role;
    pending_.seqId = seqId;
    pending_.retries = 0;
    pending_.active = true;
    pending_.timer.setTimer(kAckTimeoutMs);
    retryStats_.sends++;

    wirelessManager_->sendEspNowData(
        supporterPeer, PktType::kRoleAnnounce,
        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
}

// Fire-and-forget, asymmetric with broadcastRoleAndChampion which has
// ACK+retry on the supporter-jack direction. Rationale: losing this packet
// leaves the opponent momentarily uncertain of our role, but their own
// onChainStateChanged will fire whenever their topology shifts (including
// our handshake completion) and trigger a fresh broadcast toward us. So the
// system is self-healing on any real topology change. Adding ACK+retry here
// would double the airtime cost of every chain change; the relaxed model is
// acceptable given the healing path.
void ChainDuelManager::sendRoleToOpponentJack() {
    const uint8_t* opponentPeer = rdc_->getPeerMac(opponentJack());
    if (opponentPeer == nullptr) return;

    uint8_t seqId = nextRoleAnnounceSeqId_++;
    if (nextRoleAnnounceSeqId_ == 0) nextRoleAnnounceSeqId_ = 1;

    RoleAnnouncePayload payload{};
    payload.role = player_->isHunter() ? 1 : 0;
    // championMac is a placeholder — receiver ignores unless same-role peer.
    if (championMac_.has_value()) {
        memcpy(payload.championMac, championMac_->data(), 6);
    }
    payload.seqId = seqId;

    wirelessManager_->sendEspNowData(
        opponentPeer, PktType::kRoleAnnounce,
        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
}

void ChainDuelManager::onRoleAnnounceAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (!pending_.active || pending_.seqId != seqId) return;
    if (memcmp(fromMac, pending_.targetMac.data(), 6) != 0) return;
    retryStats_.ackLatencyMsSum += pending_.timer.getElapsedTime();
    retryStats_.ackCount++;
    pending_.active = false;
}

void ChainDuelManager::sync() {
    // 1) Role-announce retry (single-slot, supporter-jack direction).
    if (pending_.active && pending_.timer.expired()) {
        if (pending_.retries >= kMaxRetries) {
            const uint8_t* t = pending_.targetMac.data();
            LOG_W(TAG,
                "kRoleAnnounce abandoned after %u retries: target=%02X:%02X:%02X:%02X:%02X:%02X seqId=%u",
                (unsigned)kMaxRetries,
                t[0], t[1], t[2], t[3], t[4], t[5],
                (unsigned)pending_.seqId);
            retryStats_.abandons++;
            pending_.active = false;
        } else {
            pending_.retries++;
            retryStats_.retries++;
            RoleAnnouncePayload payload{};
            payload.role = pending_.role;
            memcpy(payload.championMac, pending_.championMac.data(), 6);
            payload.seqId = pending_.seqId;
            wirelessManager_->sendEspNowData(
                pending_.targetMac.data(), PktType::kRoleAnnounce,
                reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
            // Exponential backoff between retransmits: 200, 400, 800ms
            // (kMaxRetries=3). See RDC sync() for rationale (async driver
            // queue needs drain time between retries).
            pending_.timer.setTimer(kAckTimeoutMs << pending_.retries);
        }
    }

    // 2) Per-supporter WIN/LOSS game-event retries. Champion-side only.
    for (size_t i = 0; i < pendingGameEvents_.size(); ) {
        PendingGameEvent& pending = pendingGameEvents_[i];
        if (!pending.timer.expired()) { ++i; continue; }

        if (pending.retries >= kMaxRetries) {
            const uint8_t* t = pending.targetMac.data();
            LOG_W(TAG,
                "kChainGameEvent abandoned after %u retries: target=%02X:%02X:%02X:%02X:%02X:%02X seqId=%u event=%u",
                (unsigned)kMaxRetries,
                t[0], t[1], t[2], t[3], t[4], t[5],
                (unsigned)pending.seqId,
                (unsigned)pending.eventType);
            retryStats_.abandons++;
            pendingGameEvents_.erase(pendingGameEvents_.begin() + i);
            continue;  // do not increment i; vector shifted
        }

        pending.retries++;
        retryStats_.retries++;
        ChainGameEventPayload payload{};
        payload.event_type = pending.eventType;
        payload.seqId = pending.seqId;
        wirelessManager_->sendEspNowData(
            pending.targetMac.data(), PktType::kChainGameEvent,
            reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
        pending.timer.setTimer(kAckTimeoutMs << pending.retries);
        ++i;
    }
}
