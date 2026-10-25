#include "game/chain-duel-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"

#define TAG "CDM"

ChainDuelManager::ChainDuelManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
    : player(player)
    , wirelessManager(wirelessManager)
    , rdc(rdc)
    , resender(wirelessManager)
    // No abandon handler: a role announce that ran out its span is repaired by the
    // backstop in sync(), not by a callback.
    , roleAnnounceChannel(wirelessManager, &resender, PktType::kRoleAnnounce, nullptr)
    , chainConfirmChannel(wirelessManager, &resender, PktType::kChainConfirm, nullptr)
    , chainJoinChannel(wirelessManager, &resender, PktType::kChainJoin, nullptr) {
    chainConfirmChannel.onReceive([this](const uint8_t*, const ChainConfirmPayload& p) {
        onConfirmReceived(p.originatorMac);
    });
    chainJoinChannel.onReceive([this](const uint8_t* fromMac, const ChainJoinPayload& p) {
        onChainJoinReceived(fromMac, p.championMac);
    });
    // The radio's delivery report is this channel's ack; there is no reply
    // packet. Latency is read here because the channel reports delivery but does
    // not time it, and the peer is recorded as told here for the same reason:
    // this is the first moment it is true.
    // Receive rides the channel too, so the length check, the decode and the
    // duplicate suppression are the channel's rather than a second copy of them
    // in whoever routes the packet.
    roleAnnounceChannel.onReceive([this](const uint8_t* fromMac, const RoleAnnouncePayload& p) {
        onRoleAnnounceReceived(fromMac, p.championMac);
    });
    roleAnnounceChannel.setOnDelivered([this](uint8_t seqId, const uint8_t* mac) {
        ackLatencyMsSum += roleAnnounceSentTimer.getElapsedTime();
        ackCount++;
        recordAnnounceDelivered(seqId, mac);
    });
    // Subscribed here rather than by whoever builds this manager: an owner that
    // wires it is an owner every other caller has to imitate, and one that forgets
    // gets a manager that compiles, runs, and silently never reacts.
    rdc->setChainChangeCallback([this]() { onChainStateChanged(); });
    rdc->setOnContextReceived([this](SerialIdentifier, DeviceType, const uint8_t*, size_t) {
        if (supporterAnnounce.has_value()) {
            roleAnnounceChannel.cancel(supporterAnnounce->peer.data());
            supporterAnnounce.reset();
        }
        onChainStateChanged();
    });
    // The ring latch moves with both cables still seated — applyUpstreamHead sets
    // and clears it off the HELLO parse, no jack edge — so a standing confirm has
    // to be re-sent from the role edge; no jack edge would carry it.
    rdc->setOnChainRoleChange([this](ChainRole) {
        if (isLoop() != wasInLoop) onChainStateChanged();
        resendConfirm();
    });
    wasInLoop = isLoop();
    // Armed here because SimpleTimer::expired() is false while unarmed, so an
    // unarmed backstop never fires at all.
    roleAnnounceBackstopTimer.setTimer(ROLE_ANNOUNCE_BACKSTOP_MS);
}

ChainDuelManager::~ChainDuelManager() {
    rdc->setChainChangeCallback(nullptr);
    rdc->setOnContextReceived(nullptr);
    rdc->setOnChainRoleChange(nullptr);
}

SerialIdentifier ChainDuelManager::opponentJack() const {
    return player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
}

SerialIdentifier ChainDuelManager::supporterJack() const {
    return player->isHunter() ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
}

std::optional<bool> ChainDuelManager::peerIsHunter(SerialIdentifier port) const {
    if (rdc->getPeerDeviceType(port) != DeviceType::PDN) return std::nullopt;
    size_t len = 0;
    const uint8_t* raw = rdc->getPeerProfile(port, len);
    if (raw == nullptr || len < sizeof(PlayerProfile)) return std::nullopt;
    return reinterpret_cast<const PlayerProfile*>(raw)->gameRole != 0;
}

bool ChainDuelManager::containsMac(const MacSlots& slots, size_t count, const uint8_t* mac) {
    for (size_t i = 0; i < count; i++) {
        if (memcmp(slots[i].data(), mac, 6) == 0) return true;
    }
    return false;
}

void ChainDuelManager::recordMac(MacSlots& slots, std::atomic<size_t>& count,
                                 size_t& writeIndex, const uint8_t* mac) {
    size_t used = count.load();
    if (containsMac(slots, used, mac)) return;
    if (used < MAX_CHAIN_SUPPORTERS) {
        memcpy(slots[used].data(), mac, 6);
        count.store(used + 1);
        return;
    }
    memcpy(slots[writeIndex].data(), mac, 6);
    writeIndex = (writeIndex + 1) % MAX_CHAIN_SUPPORTERS;
}

bool ChainDuelManager::isLoop() const {
    // Ring membership is the RDC's fact: every device on a closed loop reads true
    // here, not only the one that detected the closure.
    return rdc->isInRing();
}

bool ChainDuelManager::isSupporter() const {
    auto opponentRole = peerIsHunter(opponentJack());
    if (!opponentRole.has_value()) return false;
    if (*opponentRole != player->isHunter()) return false;
    return !isLoop();
}

bool ChainDuelManager::isChampion() const {
    // Default-champion: head of own-role chain. Demoted to supporter only
    // when a same-role peer sits on our opponent jack. Rings resolved by
    // isLoop() check (no champion inside a ring).
    return !isSupporter() && !isLoop();
}

bool ChainDuelManager::canInitiateMatch() const {
    if (!player->isHunter()) return false;
    // A closed ring is the shootout's topology; no 1v1 pairing forms inside one.
    if (isLoop()) return false;
    // Half-open gate. Connected means the peer's context landed over the radio,
    // so a path back exists. A jack that only reached Connecting has a peer MAC
    // off one inbound serial frame and nothing proving the peer can answer, and
    // a match pushed across it strands the initiator waiting for an ack.
    if (rdc->getPortStatus(opponentJack()) != PortStatus::CONNECTED) return false;
    return peerIsHunter(opponentJack()) == false;
}

std::vector<std::array<uint8_t, 6>> ChainDuelManager::getSupporterChainPeers() const {
    if (isLoop()) return {};
    // A cable is the strongest evidence of membership there is, so the device on
    // our own supporter jack never has to announce itself to be counted.
    const uint8_t* directPeer = rdc->getPeerMac(supporterJack());
    if (directPeer == nullptr || peerIsHunter(supporterJack()) != player->isHunter()) return {};
    std::vector<std::array<uint8_t, 6>> peers;
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), directPeer, 6);
    peers.push_back(mac);
    size_t joined = supporterRosterCount.load();
    for (size_t i = 0; i < joined; i++) {
        if (memcmp(supporterRoster[i].data(), directPeer, 6) == 0) continue;
        peers.push_back(supporterRoster[i]);
    }
    return peers;
}

bool ChainDuelManager::isEventFromOwnChampion(const uint8_t* eventChampionMac) const {
    const uint8_t* own = getChampionMac();
    return own != nullptr && memcmp(own, eventChampionMac, 6) == 0;
}

void ChainDuelManager::sendGameEventToSupporters(ChainGameEventType eventType) {
    if (!isChampion()) return;

    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) {
        LOG_E(TAG, "chain game event dropped: no local MAC");
        return;
    }

    // Supporters only gate the send and seed the ack tally; the frame itself is
    // addressed to nobody in particular.
    std::vector<std::array<uint8_t, 6>> peers = getSupporterChainPeers();
    if (peers.empty()) return;

    ChainGameEventPayload payload{};
    payload.event_type = static_cast<uint8_t>(eventType);
    memcpy(payload.championMac, selfMac, 6);

    // WIN/LOSS are state-terminal for the supporter UI and must arrive or
    // the supporter display sticks on a stale screen until the next chain
    // event. They get seqIds and retry tracking.
    //
    // COUNTDOWN/DRAW are time-sensitive transient events. A late-arriving
    // retry of COUNTDOWN would falsely re-arm a supporter whose duel has
    // already resolved; a late DRAW would disarm a supporter who just
    // entered a new COUNTDOWN. Keep them fire-and-forget: seqId=0.
    if (eventType == ChainGameEventType::WIN || eventType == ChainGameEventType::LOSS) {
        payload.seqId = nextGameEventSeqId++;
        if (nextGameEventSeqId == 0) nextGameEventSeqId = 1;
        gameEventSentTimer.setTimer(0);
        // A newer terminal event obsoletes the previous one for every supporter
        // at once: the supporter's screen shows the latest result, so an older
        // retransmit landing afterwards would flip it back. Dropping the prior
        // fan-out is what SendMode::SUPERSEDE_PER_TARGET does for unicast.
        resender.cancelAll(PktType::kChainGameEvent);
        resender.sendBroadcast(peers, PktType::kChainGameEvent, payload.seqId,
                               reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
    } else {
        // Fire-and-forget: no seqId, no ack owed, so it goes straight out. One
        // broadcast frame either way — addressing a supporter three cables away
        // by unicast costs a peer-table slot per device in the chain and the
        // table holds 20. Supporters keep only the events whose championMac
        // matches the champion they follow.
        wirelessManager->sendEspNowData(
            wirelessManager->getBroadcastAddress(),
            PktType::kChainGameEvent,
            reinterpret_cast<const uint8_t*>(&payload),
            sizeof(payload));
    }

    // After the send, never before: the recipients above are the join roster and
    // not this roll call, but wiping first is one edit away from dropping every
    // multi-hop supporter out of the COUNTDOWN that tells them to press.
    if (eventType == ChainGameEventType::COUNTDOWN) {
        clearSupporterConfirms();
    }
}

void ChainDuelManager::sendGameEventAck(const uint8_t* toMac, uint8_t seqId) {
    if (toMac == nullptr || seqId == 0) return;
    ChainGameEventAckPayload ack{};
    ack.seqId = seqId;
    wirelessManager->sendEspNowData(
        toMac, PktType::kChainGameEventAck,
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

void ChainDuelManager::onChainGameEventAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (fromMac == nullptr || seqId == 0) return;
    // A broadcast carries no per-member delivery evidence from the radio, so
    // this reply is the only thing that clears a supporter's slot.
    if (resender.onAck(PktType::kChainGameEvent, seqId, fromMac)) {
        ackLatencyMsSum += gameEventSentTimer.getElapsedTime();
        ackCount++;
    }
}

void ChainDuelManager::sendConfirm() {
    // Latched before the champion check, not after: a press that lands before
    // the role cascade has named a champion still has to reach whoever the
    // cascade names, and that is what resendConfirm is for.
    confirmSent = true;

    const uint8_t* champ = getChampionMac();
    if (champ == nullptr) return;

    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;

    ChainConfirmPayload payload{};
    memcpy(payload.originatorMac, selfMac, 6);
    chainConfirmChannel.sendReliable(champ, payload);
}

void ChainDuelManager::resendConfirm() {
    if (!confirmSent) return;
    sendConfirm();
}

void ChainDuelManager::onChainGameEventReceived(uint8_t eventType) {
    if (static_cast<ChainGameEventType>(eventType) != ChainGameEventType::COUNTDOWN) return;
    confirmSent = false;
    resender.cancelAll(PktType::kChainConfirm);
}

void ChainDuelManager::onConfirmReceived(const uint8_t* originatorMac) {
    // Recorded unconditionally. Whether this originator is a chain member, and
    // whether we are the champion who gets to count it, are both read live in
    // getConfirmedSupporterCount — neither is knowable for certain at the moment
    // a press arrives.
    recordMac(receivedConfirms, receivedConfirmCount, receivedConfirmWrite, originatorMac);
}

void ChainDuelManager::onChainJoinReceived(const uint8_t* supporterMac,
                                           const uint8_t* joinChampionMac) {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) {
        LOG_E(TAG, "chain join dropped: no local MAC");
        return;
    }
    // A join that names another champion reached us by radio accident; enrolling
    // its sender would hand this device a supporter from someone else's chain.
    if (memcmp(joinChampionMac, selfMac, 6) != 0) return;
    recordMac(supporterRoster, supporterRosterCount, supporterRosterWrite, supporterMac);
}

void ChainDuelManager::announceToChampion() {
    const uint8_t* champ = getChampionMac();
    if (champ == nullptr) return;
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;
    // A champion is its own chain's root, not a member of it.
    if (memcmp(champ, selfMac, 6) == 0) return;

    ChainJoinPayload payload{};
    memcpy(payload.championMac, champ, 6);
    chainJoinChannel.sendReliable(champ, payload);
}

void ChainDuelManager::onChainStateChanged() {
    wasInLoop = isLoop();

    // Losing the supporter-jack cable strands the entire chain below it, however
    // deep, so both the roll call and the roster it is scored against go with it.
    size_t count = (rdc->getPeerMac(supporterJack()) != nullptr &&
                    peerIsHunter(supporterJack()) == player->isHunter())
                       ? 1u
                       : 0u;
    if (lastSupporterChainCount > 0 && count == 0) {
        clearSupporterConfirms();
        supporterRosterCount.store(0);
        supporterRosterWrite = 0;
    }
    lastSupporterChainCount = count;

    if (isChampion()) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr) {
            std::array<uint8_t, 6> selfArr;
            memcpy(selfArr.data(), selfMac, 6);
            championMac = selfArr;
        }
    } else if (isSupporter()) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (championMac.has_value() && selfMac != nullptr &&
            memcmp(championMac->data(), selfMac, 6) == 0) {
            championMac.reset();
        }
    } else {
        championMac.reset();
    }

    broadcastRoleAndChampion();

    if (isSupporter()) {
        // Re-send on every topology event: join has no backstop once its retry span expires.
        announceToChampion();
    } else {
        // Clear confirmSent so patching into another chain mid-round cannot forward a prior press.
        confirmSent = false;
        resender.cancelAll(PktType::kChainConfirm);
        resender.cancelAll(PktType::kChainJoin);
    }
}

unsigned long ChainDuelManager::getBoostMs() const {
    return getConfirmedSupporterCount() * BOOST_PER_SUPPORTER_MS;
}

size_t ChainDuelManager::getConfirmedSupporterCount() const {
    if (!isChampion()) return 0;
    std::vector<std::array<uint8_t, 6>> peers = getSupporterChainPeers();
    size_t count = receivedConfirmCount.load();
    size_t confirmed = 0;
    for (size_t i = 0; i < count; i++) {
        for (const std::array<uint8_t, 6>& peer : peers) {
            if (memcmp(peer.data(), receivedConfirms[i].data(), 6) == 0) {
                confirmed++;
                break;
            }
        }
    }
    return confirmed;
}

const uint8_t* ChainDuelManager::getChampionMac() const {
    if (isLoop()) return nullptr;
    return championMac.has_value() ? championMac->data() : nullptr;
}

void ChainDuelManager::clearSupporterConfirms() {
    receivedConfirmCount.store(0);
    receivedConfirmWrite = 0;
}

void ChainDuelManager::onRoleAnnounceReceived(
    const uint8_t* fromMac,
    const uint8_t* announcedChampionMac) {
    const uint8_t* opponentPeer = rdc->getPeerMac(opponentJack());
    if (opponentPeer == nullptr || memcmp(opponentPeer, fromMac, 6) != 0) return;
    if (!isSupporter()) return;

    // Wire input: the field is always present, so a sender with no champion sends
    // it zeroed, and a two-bounty ring does exactly that: both clear their champion,
    // then each announces to the other's opponent jack with a matching role. Caching
    // the zero would cascade it onward as this device's champion.
    if (MacToUInt64(announcedChampionMac) == 0) {
        championMac.reset();
        resender.cancelAll(PktType::kChainConfirm);
        resender.cancelAll(PktType::kChainJoin);
        broadcastRoleAndChampion();
        return;
    }

    std::array<uint8_t, 6> newMac;
    memcpy(newMac.data(), announcedChampionMac, 6);
    bool changed = !championMac.has_value() || *championMac != newMac;
    championMac = newMac;
    if (changed) {
        resender.cancelAll(PktType::kChainConfirm);
        resender.cancelAll(PktType::kChainJoin);
    }
    if (changed || (supporterAnnounce.has_value() && supporterAnnounce->delivered)) {
        supporterAnnounce.reset();
        broadcastRoleAndChampion();
    }
    // A head transfer swaps the champion without touching this device's own
    // links, so nothing else here would tell the new champion that this
    // supporter exists, let alone that it is already in.
    announceToChampion();
    resendConfirm();
}

void ChainDuelManager::recordAnnounceDelivered(uint8_t seqId, const uint8_t* mac) {
    if (mac == nullptr || !supporterAnnounce.has_value()) return;
    if (supporterAnnounce->seqId != seqId ||
        memcmp(supporterAnnounce->peer.data(), mac, 6) != 0) {
        return;
    }
    supporterAnnounce->delivered = true;
}

void ChainDuelManager::broadcastRoleAndChampion() {
    const uint8_t* supporterPeer = rdc->getPeerMac(supporterJack());
    const uint8_t* champ = getChampionMac();
    if (supporterAnnounce.has_value() &&
        (supporterPeer == nullptr || champ == nullptr ||
         memcmp(supporterAnnounce->peer.data(), supporterPeer, 6) != 0)) {
        roleAnnounceChannel.cancel(supporterAnnounce->peer.data());
        supporterAnnounce.reset();
    }
    if (supporterPeer == nullptr || champ == nullptr) return;

    RoleAnnounceState content;
    memcpy(content.peer.data(), supporterPeer, 6);
    memcpy(content.champion.data(), champ, 6);
    if (supporterAnnounce.has_value() && supporterAnnounce->told(content)) return;

    // Half-open gate, same reasoning as canInitiateMatch. A jack at Connecting has
    // a peer MAC off one inbound serial frame; the supporter has not necessarily
    // parsed ours yet, and it drops announces from a MAC it does not yet hold as a
    // direct peer. The radio would still report the frame delivered, so the retry
    // clears and the announce is simply lost. Connected means its context came back
    // over the radio, which it could only send having already recorded us.
    if (rdc->getPortStatus(supporterJack()) != PortStatus::CONNECTED) return;

    RoleAnnouncePayload payload{};
    memcpy(payload.championMac, content.champion.data(), 6);

    roleAnnounceSentTimer.setTimer(0);
    content.seqId = roleAnnounceChannel.sendReliable(supporterPeer, payload);
    supporterAnnounce = content;
}

void ChainDuelManager::sync() {
    // Non-latching members learn of a ring from HELLO with both jacks already
    // connected, so neither the jack nor the role callback fires.
    if (isLoop() != wasInLoop) {
        onChainStateChanged();
    }

    // The role announce clears on SEND_SUCCESS; the WIN/LOSS fan-out clears one
    // supporter at a time, since a broadcast carries no per-member evidence.
    resender.sync();

    // Backstop for the supporter announce. A settled chain raises no chain-state
    // events, so one that ran out its span has no other way back — and losing it
    // leaves the supporter without a champion. Cheap once delivered; the stamp
    // gates it.
    if (roleAnnounceBackstopTimer.expired()) {
        roleAnnounceBackstopTimer.setTimer(ROLE_ANNOUNCE_BACKSTOP_MS);
        broadcastRoleAndChampion();
    }
}
