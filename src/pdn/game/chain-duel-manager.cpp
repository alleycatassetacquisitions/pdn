#include "game/chain-duel-manager.hpp"
#include "device/drivers/logger.hpp"

#define TAG "CDM"

ChainDuelManager::ChainDuelManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
    : player(player)
    , wirelessManager(wirelessManager)
    , rdc(rdc)
    , resender(wirelessManager, Resender::BudgetPolicy::EVERY_ROUND)
    // No abandon handler: routing one in requires a ReliableTransport, and this
    // channel is bound straight to a local Resender. A role announce that runs
    // out of retries is repaired by the backstop in sync(), not by a callback.
    , roleAnnounceChannel(wirelessManager, &resender, PktType::kRoleAnnounce, nullptr) {
    // The radio's delivery report is this channel's ack; there is no reply
    // packet. Latency is read here because the channel reports delivery but does
    // not time it, and the peer is recorded as told here for the same reason:
    // this is the first moment it is true.
    // Receive rides the channel too, so the length check, the decode and the
    // duplicate suppression are the channel's rather than a second copy of them
    // in whoever routes the packet. Duplicates were already harmless here —
    // onRoleAnnounceReceived is idempotent — but that made the tx half the only
    // half using the channel it owns.
    roleAnnounceChannel.onReceive([this](const uint8_t* fromMac, const RoleAnnouncePayload& p) {
        onRoleAnnounceReceived(fromMac, p.role, p.championMac, p.seqId);
    });
    roleAnnounceChannel.setOnDelivered([this](uint8_t, const uint8_t* mac) {
        ackLatencyMsSum += roleAnnounceSentTimer.getElapsedTime();
        ackCount++;
        recordAnnounceDelivered(mac);
    });
    // Subscribed here rather than by whoever builds this manager: an owner that
    // wires it is an owner every other caller has to imitate, and one that forgets
    // gets a manager that compiles, runs, and silently never reacts.
    rdc->setChainChangeCallback([this]() { onChainStateChanged(); });
    // The ring latch moves with both cables still seated — applyUpstreamHead sets
    // and clears it off the HELLO parse, no jack edge — so a standing confirm has
    // to be re-sent from the role edge; no jack edge would carry it.
    rdc->setOnChainRoleChange([this](ChainRole) { resendConfirm(); });

    // The channel installs its own send-status handler, which is what clears a
    // role-announce retry: this channel has no reply packet, so the radio's
    // SEND_SUCCESS is the only delivery signal there is.
}

ChainDuelManager::~ChainDuelManager() {
    rdc->setChainChangeCallback(nullptr);
    rdc->setOnChainRoleChange(nullptr);
}

SerialIdentifier ChainDuelManager::opponentJack() const {
    return player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
}

SerialIdentifier ChainDuelManager::supporterJack() const {
    return player->isHunter() ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
}

std::optional<bool> ChainDuelManager::peerIsHunter(SerialIdentifier port) const {
    return peerRoleByPort[port == SerialIdentifier::INPUT_JACK ? 0 : 1];
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
    if (rdc->getPeerDeviceType(opponentJack()) != DeviceType::PDN) return false;
    std::optional<bool> opponentRole = peerIsHunter(opponentJack());
    if (!opponentRole.has_value()) return false;
    return *opponentRole != player->isHunter();
}

std::vector<std::array<uint8_t, 6>> ChainDuelManager::getSupporterChainPeers() const {
    if (isLoop()) return {};
    std::vector<std::array<uint8_t, 6>> peers;
    // A cable is the strongest evidence of membership there is, so the device on
    // our own supporter jack never has to announce itself to be counted.
    const uint8_t* directPeer = rdc->getPeerMac(supporterJack());
    if (directPeer != nullptr) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), directPeer, 6);
        peers.push_back(mac);
    }
    size_t joined = supporterRosterCount.load();
    for (size_t i = 0; i < joined; i++) {
        if (directPeer != nullptr && memcmp(supporterRoster[i].data(), directPeer, 6) == 0) continue;
        peers.push_back(supporterRoster[i]);
    }
    return peers;
}

bool ChainDuelManager::isEventFromOwnChampion(const uint8_t* eventChampionMac) const {
    return championMac.has_value() && memcmp(championMac->data(), eventChampionMac, 6) == 0;
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

    if (!championMac.has_value()) return;

    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;

    ChainConfirmPayload payload{};
    memcpy(payload.originatorMac, selfMac, 6);
    payload.seqId = nextConfirmSeqId++;
    if (nextConfirmSeqId == 0) nextConfirmSeqId = 1;

    wirelessManager->sendEspNowData(
        championMac->data(),
        PktType::kChainConfirm,
        reinterpret_cast<const uint8_t*>(&payload),
        sizeof(payload));
}

void ChainDuelManager::resendConfirm() {
    if (!confirmSent) return;
    sendConfirm();
}

void ChainDuelManager::onChainGameEventReceived(uint8_t eventType) {
    if (static_cast<ChainGameEventType>(eventType) != ChainGameEventType::COUNTDOWN) return;
    confirmSent = false;
}

void ChainDuelManager::onConfirmReceived(
    const uint8_t* fromMac,
    const uint8_t* originatorMac,
    uint8_t seqId) {
    (void)fromMac; (void)seqId;
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
    if (!championMac.has_value()) return;
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;
    // A champion is its own chain's root, not a member of it.
    if (memcmp(championMac->data(), selfMac, 6) == 0) return;

    ChainJoinPayload payload{};
    memcpy(payload.championMac, championMac->data(), 6);
    wirelessManager->sendEspNowData(
        championMac->data(),
        PktType::kChainJoin,
        reinterpret_cast<const uint8_t*>(&payload),
        sizeof(payload));
}

void ChainDuelManager::onChainStateChanged() {
    applyChainStateChange();

    // Leaving the supporter role voids the standing confirm. Without this, a
    // device unplugged mid-round and patched into another chain would re-send a
    // press it made to a champion it no longer follows.
    if (!isSupporter()) confirmSent = false;
}

void ChainDuelManager::applyChainStateChange() {
    // Losing the supporter-jack cable strands the entire chain below it, however
    // deep, so both the roll call and the roster it is scored against go with it.
    size_t count = rdc->getPeerMac(supporterJack()) != nullptr ? 1u : 0u;
    if (lastSupporterChainCount > 0 && count == 0) {
        clearSupporterConfirms();
        supporterRosterCount.store(0);
        supporterRosterWrite = 0;
    }
    lastSupporterChainCount = count;

    // Drop cached peer roles for any port that no longer has a direct peer.
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        if (rdc->getPeerMac(port) == nullptr) {
            peerRoleByPort[port == SerialIdentifier::INPUT_JACK ? 0 : 1].reset();
        }
    }

    if (isChampion()) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr) {
            std::array<uint8_t, 6> selfArr;
            memcpy(selfArr.data(), selfMac, 6);
            if (!championMac.has_value() || *championMac != selfArr) {
                championMac = selfArr;
                broadcastRoleAndChampion();
                sendRoleToOpponentJack();
                return;
            }
        }
    }

    // If we've become a supporter but still hold our own MAC as champion,
    // invalidate it — the real champion's announce will repopulate via
    // onRoleAnnounceReceived.
    if (isSupporter() && championMac.has_value()) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr && memcmp(championMac->data(), selfMac, 6) == 0) {
            championMac.reset();
        }
    }

    // If a new supporter-jack direct peer has appeared since our last announce,
    // broadcast the current championMac to it. Guard against re-firing on every
    // chain-state event (which would cause packet storms and destabilize the
    // serial heartbeat timing).
    const uint8_t* supporterPeer = rdc->getPeerMac(supporterJack());
    if (championMac.has_value() && supporterPeer != nullptr) {
        std::array<uint8_t, 6> cur;
        memcpy(cur.data(), supporterPeer, 6);
        // Deliberately not also skipped while a send is in flight, unlike the
        // opponent side. That guard is only safe where something retries on a
        // timer: here the chain-state event IS the only trigger, so suppressing
        // one is permanent. Re-sending inside the delivery window costs at most a
        // duplicate frame, and SUPERSEDE_PER_TARGET means it replaces rather than
        // accumulates.
        if (!lastAnnouncedSupporterJackMac.has_value() || *lastAnnouncedSupporterJackMac != cur) {
            broadcastRoleAndChampion();
        }
    } else if (supporterPeer == nullptr) {
        // Any chain change with nothing on the supporter jack lands here. What
        // the coordinator's post-teardown report adds is that a departure lands
        // here too: reported only from the dismount, this jack would still name
        // its departing peer, and a peer returning on the same MAC would match
        // the stamp and never be announced to again.
        lastAnnouncedSupporterJackMac.reset();
    }

    sendRoleToOpponentJack();

    // Re-offered on every topology event, not only when the champion changes: the
    // join is unacknowledged, and a dropped one costs this device's press for the
    // whole round with nothing else to repair it.
    announceToChampion();
}

void ChainDuelManager::setPeerRole(SerialIdentifier port, bool isHunter) {
    peerRoleByPort[port == SerialIdentifier::INPUT_JACK ? 0 : 1] = isHunter;
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
    return championMac.has_value() ? championMac->data() : nullptr;
}

void ChainDuelManager::clearSupporterConfirms() {
    receivedConfirmCount.store(0);
    receivedConfirmWrite = 0;
}

void ChainDuelManager::onRoleAnnounceReceived(
    const uint8_t* fromMac,
    uint8_t role,
    const uint8_t* announcedChampionMac,
    uint8_t seqId) {
    // 1. Update peer role based on which jack fromMac is on.
    //    Also remember whether this announce came from our opponent-jack
    //    (parent) direction — only opponent-jack announces authoritatively
    //    update our championMac cache.
    bool fromOpponentJack = false;
    bool fromKnownDirectPeer = false;
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const uint8_t* directMac = rdc->getPeerMac(port);
        if (directMac != nullptr && memcmp(directMac, fromMac, 6) == 0) {
            setPeerRole(port, role == 1);
            fromKnownDirectPeer = true;
            if (port == opponentJack()) {
                fromOpponentJack = true;
            }
            break;
        }
    }

    // 2. Only act on announces from known direct peers. A stranger in radio
    //    range must not be able to move this device's champion.
    if (!fromKnownDirectPeer) return;

    // 3 & 4. Only same-role opponent-jack announces authoritatively update
    // championMac. Opposite-role senders are dueling opponents, not chain
    // parents — their championMac is irrelevant.
    if (!fromOpponentJack) return;
    if (role != (player->isHunter() ? 1u : 0u)) return;

    // 3. Register champion as ESP-NOW peer (only if it's not our own MAC).
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    bool championIsSelf = (selfMac != nullptr &&
                           memcmp(selfMac, announcedChampionMac, 6) == 0);
    if (!championIsSelf) {
        rdc->registerPeer(announcedChampionMac);
    }

    // 4. Update championMac and cascade if changed. On change, release the
    // ESP-NOW peer slot held by the OLD champion MAC unless it's still
    // reachable via one of our jacks (then its registration is owned by the
    // chain-peer bookkeeping and will be cleaned up when that list changes).
    std::array<uint8_t, 6> newMac;
    memcpy(newMac.data(), announcedChampionMac, 6);
    bool changed = !championMac.has_value() || *championMac != newMac;
    if (changed && championMac.has_value()) {
        std::array<uint8_t, 6> oldMac = *championMac;
        bool oldIsSelf = (selfMac != nullptr &&
                          memcmp(selfMac, oldMac.data(), 6) == 0);
        if (!oldIsSelf) {
            if (!rdc->isDirectPeer(oldMac.data())) {
                rdc->unregisterPeer(oldMac.data());
            }
        }
    }
    championMac = newMac;
    if (changed) {
        broadcastRoleAndChampion();
        // A head transfer swaps the champion without touching this device's own
        // links, so nothing else here would tell the new champion that this
        // supporter exists, let alone that it is already in.
        announceToChampion();
        resendConfirm();
    }
}

void ChainDuelManager::recordAnnounceDelivered(const uint8_t* mac) {
    if (mac == nullptr) return;
    // Whichever jack faces this peer has now been told. In a 2-node ring both
    // do, and both stamps are set — correctly, since both announces went there.
    std::array<uint8_t, 6> cur;
    memcpy(cur.data(), mac, 6);
    const uint8_t* supporterPeer = rdc->getPeerMac(supporterJack());
    if (supporterPeer != nullptr && memcmp(supporterPeer, mac, 6) == 0) {
        lastAnnouncedSupporterJackMac = cur;
    }
    const uint8_t* opponentPeer = rdc->getPeerMac(opponentJack());
    if (opponentPeer != nullptr && memcmp(opponentPeer, mac, 6) == 0) {
        lastAnnouncedOpponentJackMac = cur;
    }
}

bool ChainDuelManager::broadcastRoleAndChampion() {
    if (!championMac.has_value()) return false;

    const uint8_t* supporterPeer = rdc->getPeerMac(supporterJack());
    if (supporterPeer == nullptr) return false;
    // Half-open gate, same reasoning as canInitiateMatch. A jack at Connecting has
    // a peer MAC off one inbound serial frame; the supporter has not necessarily
    // parsed ours yet, and it drops announces from a MAC it does not yet hold as a
    // direct peer. The radio would still report the frame delivered, so the retry
    // clears and the announce is simply lost. Connected means its context came back
    // over the radio, which it could only send having already recorded us. Reaching
    // Connected fires a jack change, so the cascade re-sends this then.
    if (rdc->getPortStatus(supporterJack()) != PortStatus::CONNECTED) return false;

    RoleAnnouncePayload payload{};
    payload.role = player->isHunter() ? 1 : 0;
    memcpy(payload.championMac, championMac->data(), 6);

    roleAnnounceSentTimer.setTimer(0);
    roleAnnounceChannel.sendReliable(supporterPeer, payload);
    return true;
}

// This announce is the only thing that tells the opponent what role we hold, and
// their canInitiateMatch refuses a duel until they know it. Nothing repairs a
// lost one: their own announce to us populates OUR view of THEM, not theirs of
// us, and the caller below re-sends only when the peer MAC changes — which it
// will not, because the cable did not move. So it is retried like the
// supporter-side announce, off the radio's delivery report rather than a reply
// packet, which costs no extra airtime unless a frame actually fails.
bool ChainDuelManager::sendRoleToOpponentJack() {
    const uint8_t* opponentPeer = rdc->getPeerMac(opponentJack());
    if (opponentPeer == nullptr) {
        // Forget who was last told, so a cable returning on the same MAC is
        // announced to again rather than matching a stamp left by the old link.
        lastAnnouncedOpponentJackMac.reset();
        return false;
    }
    std::array<uint8_t, 6> cur;
    memcpy(cur.data(), opponentPeer, 6);
    // Already told, or still telling. "Told" means the radio confirmed delivery,
    // not that a frame was handed over — see recordAnnounceDelivered. Holding
    // off while a send is still pending keeps the backstop below from stacking a
    // second announce on top of one that is simply still in its retry window.
    if (lastAnnouncedOpponentJackMac.has_value() && *lastAnnouncedOpponentJackMac == cur) {
        return false;
    }
    if (resender.isPending(PktType::kRoleAnnounce, opponentPeer)) return false;
    // Same half-open gate as the supporter side: a jack at Connecting has a peer
    // MAC off one inbound frame, and the peer drops announces from a MAC it has
    // not yet recorded.
    if (rdc->getPortStatus(opponentJack()) != PortStatus::CONNECTED) return false;

    RoleAnnouncePayload payload{};
    payload.role = player->isHunter() ? 1 : 0;
    // championMac is a placeholder — receiver ignores unless same-role peer.
    if (championMac.has_value()) {
        memcpy(payload.championMac, championMac->data(), 6);
    }

    roleAnnounceSentTimer.setTimer(0);
    roleAnnounceChannel.sendReliable(opponentPeer, payload);
    return true;
}

void ChainDuelManager::sync() {
    // Both of this manager's channels retransmit and abandon here. The role
    // announce is cleared by the radio's SEND_SUCCESS; the WIN/LOSS fan-out is
    // cleared one supporter at a time by their acks, since a broadcast carries
    // no per-member delivery evidence.
    resender.sync();

    // Self-healing backstop for the opponent announce. Once it exhausts its
    // retry budget nothing else would ever re-send it: the chain-state cascade
    // offers it only when the peer MAC changes, and a cable that did not move
    // never changes it. The opponent would then never learn this device's role,
    // and their canInitiateMatch refuses that cable a duel until it is
    // physically re-seated. The supporter side needs no equivalent — its
    // cascade re-offers on every topology event, unchanged MAC included.
    // Arms on the first tick and only repairs a full interval later: the cascade
    // is the prompt path, this is the one that runs after it has already failed.
    if (!roleAnnounceBackstopTimer.isRunning()) {
        roleAnnounceBackstopTimer.setTimer(ROLE_ANNOUNCE_BACKSTOP_MS);
    } else if (roleAnnounceBackstopTimer.expired()) {
        roleAnnounceBackstopTimer.setTimer(ROLE_ANNOUNCE_BACKSTOP_MS);
        // Cheap once delivered: the stamp matches and this returns immediately.
        sendRoleToOpponentJack();
    }
}
