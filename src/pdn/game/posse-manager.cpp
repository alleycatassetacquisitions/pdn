#include "game/posse-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"

#define TAG "POSSE"

PosseManager::PosseManager(Player* player, WirelessManager* wirelessManager,
                           RemoteDeviceCoordinator* rdc)
    : player(player)
    , wirelessManager(wirelessManager)
    , rdc(rdc)
    // No abandon handler: a role announce that ran out its span is repaired by the
    // backstop in sync(), not by a callback.
    , roleAnnounceChannel(wirelessManager, PktType::kRoleAnnounce)
    , chainConfirmChannel(wirelessManager, PktType::kChainConfirm)
    , chainJoinChannel(wirelessManager, PktType::kChainJoin)
    , gameEventChannel(wirelessManager, PktType::kChainGameEvent) {
    chainConfirmChannel.onReceive([this](const uint8_t*, const ChainConfirmPayload& p) {
        onConfirmReceived(p.originatorMac, p.playerUuid);
    });
    chainJoinChannel.onReceive([this](const uint8_t* fromMac, const ChainJoinPayload& p) {
        onChainJoinReceived(fromMac, p.challengerMac);
    });
    gameEventChannel.onReceive([this](const uint8_t* fromMac, const ChainGameEventPayload& p) {
        onGameEventFrame(fromMac, p);
    });
    // The only moment a supporter's delivery can be timed.
    gameEventChannel.setOnDelivered([this](uint8_t, const uint8_t*) {
        ackLatencyMsSum += gameEventSentTimer.getElapsedTime();
        ackCount++;
    });
    // roleAnnounceChannel: SEND_SUCCESS is its ack (no reply packet), and the
    // first moment the peer is known told.
    roleAnnounceChannel.onReceive([this](const uint8_t* fromMac, const RoleAnnouncePayload& p) {
        onRoleAnnounceReceived(fromMac, p.challengerMac);
    });
    roleAnnounceChannel.setOnDelivered([this](uint8_t seqId, const uint8_t* mac) {
        ackLatencyMsSum += roleAnnounceSentTimer.getElapsedTime();
        ackCount++;
        recordAnnounceDelivered(seqId, mac);
    });
    // Subscribed here rather than by whoever builds this manager: an owner that
    // wires it is an owner every other caller has to imitate, and one that forgets
    // gets a manager that compiles, runs, and silently never reacts.
    rdc->setChainChangeCallback([this]() { onChainStateChanged(); }, this);
    rdc->setOnContextReceived(
    [this](SerialIdentifier jack, DeviceType, const uint8_t*, size_t) {
        if (jack == supporterJack() && supporterAnnounce.has_value()) {
            roleAnnounceChannel.cancel(supporterAnnounce->peer.data());
            supporterAnnounce.reset();
        }
        onChainStateChanged();
        if (jack == opponentJack()) resendConfirm();
    },
    this);
    // The ring latch moves with both cables still seated — applyUpstreamHead sets
    // and clears it off the HELLO parse, no jack edge — so a standing confirm has
    // to be re-sent from the role edge; no jack edge would carry it.
    rdc->setOnChainRoleChange([this](ChainRole) { resendConfirm(); }, this);
    // Non-latching members learn of a ring from HELLO with both jacks already
    // connected, so neither the jack nor the role callback reports it.
    // A cable edge that also flips the ring runs onChainStateChanged twice; the repeat only
    // re-sends the supporter's join (superseded per target).
    rdc->setOnRingMembershipChange([this](bool) { onChainStateChanged(); }, this);
    // Armed here because SimpleTimer::expired() is false while unarmed, so an
    // unarmed backstop never fires at all.
    roleAnnounceBackstopTimer.setTimer(ROLE_ANNOUNCE_BACKSTOP_MS);
}

PosseManager::~PosseManager() {
    rdc->setChainChangeCallback(nullptr, this);
    rdc->setOnContextReceived(nullptr, this);
    rdc->setOnChainRoleChange(nullptr, this);
    rdc->setOnRingMembershipChange(nullptr, this);
}

SerialIdentifier PosseManager::opponentJack() const {
    return player->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
}

SerialIdentifier PosseManager::supporterJack() const {
    return player->isHunter() ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
}

std::optional<bool> PosseManager::peerIsHunter(SerialIdentifier jack) const {
    if (rdc->getPeerDeviceType(jack) != DeviceType::PDN) return std::nullopt;
    size_t len = 0;
    const uint8_t* raw = rdc->getPeerProfile(jack, len);
    if (raw == nullptr || len < sizeof(PlayerProfile)) return std::nullopt;
    return reinterpret_cast<const PlayerProfile*>(raw)->gameRole != 0;
}

bool PosseManager::containsMac(const MacSlots& slots, size_t count, const uint8_t* mac) {
    for (size_t i = 0; i < count; i++) {
        if (memcmp(slots[i].data(), mac, 6) == 0) return true;
    }
    return false;
}

void PosseManager::recordMac(MacSlots& slots, size_t& count, size_t& writeIndex,
                             const uint8_t* mac) {
    if (containsMac(slots, count, mac)) return;
    if (count < MAX_CHAIN_SUPPORTERS) {
        memcpy(slots[count].data(), mac, 6);
        count++;
        return;
    }
    memcpy(slots[writeIndex].data(), mac, 6);
    writeIndex = (writeIndex + 1) % MAX_CHAIN_SUPPORTERS;
}

bool PosseManager::isSupporter() const {
    auto opponentRole = peerIsHunter(opponentJack());
    if (!opponentRole.has_value()) return false;
    if (*opponentRole != player->isHunter()) return false;
    return !rdc->isInRing();
}

bool PosseManager::isChallenger() const {
    // Default-challenger: head of own-role chain. Demoted to supporter only
    // when a same-role peer sits on our opponent jack. Rings resolved by the
    // ring check: no challenger inside a ring.
    return !isSupporter() && !rdc->isInRing();
}

bool PosseManager::canInitiateMatch() const {
    if (!player->isHunter()) return false;
    // A closed ring is the shootout's topology; no 1v1 pairing forms inside one.
    if (rdc->isInRing()) return false;
    // Half-open gate. Connected means the peer's context landed over the radio,
    // so a path back exists. A jack that only reached Connecting has a peer MAC
    // off one inbound serial frame and nothing proving the peer can answer, and
    // a match pushed across it strands the initiator waiting for an ack.
    if (rdc->getJackStatus(opponentJack()) != JackStatus::CONNECTED) return false;
    return peerIsHunter(opponentJack()) == false;
}

std::vector<std::array<uint8_t, 6>> PosseManager::getSupporterChainPeers() const {
    if (rdc->isInRing()) return {};
    // A cable is the strongest evidence of membership there is, so the device on
    // our own supporter jack never has to announce itself to be counted.
    const uint8_t* directPeer = rdc->getPeerMac(supporterJack());
    if (directPeer == nullptr || peerIsHunter(supporterJack()) != player->isHunter()) return {};
    std::vector<std::array<uint8_t, 6>> peers;
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), directPeer, 6);
    peers.push_back(mac);
    for (size_t i = 0; i < supporterRosterCount; i++) {
        if (memcmp(supporterRoster[i].data(), directPeer, 6) == 0) continue;
        peers.push_back(supporterRoster[i]);
    }
    return peers;
}

bool PosseManager::isEventFromOwnChallenger(const uint8_t* eventChallengerMac) const {
    const uint8_t* own = getChallengerMac();
    return own != nullptr && memcmp(own, eventChallengerMac, 6) == 0;
}

void PosseManager::sendGameEventToSupporters(ChainGameEventType eventType) {
    if (!isChallenger()) return;

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
    memcpy(payload.challengerMac, selfMac, 6);

    // WIN/LOSS are state-terminal for the supporter UI and must arrive or
    // the supporter display sticks on a stale screen until the next chain
    // event. They get seqIds and retry tracking.
    //
    // COUNTDOWN/DRAW are time-sensitive transient events. A late-arriving
    // retry of COUNTDOWN would falsely re-arm a supporter whose match has
    // already resolved; a late DRAW would disarm a supporter who just
    // entered a new COUNTDOWN. Keep them fire-and-forget: seqId=0.
    if (eventType == ChainGameEventType::WIN || eventType == ChainGameEventType::LOSS) {
        gameEventSentTimer.setTimer(0);
        // A newer terminal event obsoletes the prior fan-out, so an older
        // retransmit cannot flip a supporter's result screen back.
        gameEventChannel.cancelAll();
        gameEventChannel.sendBroadcast(peers, payload);
    } else {
        // Broadcast rather than unicast: a supporter three cables away would
        // cost a peer-table slot per device in the chain, and the table holds 20.
        gameEventChannel.sendUnsequenced(wirelessManager->getBroadcastAddress(), payload);
    }

    // After the send, never before: the recipients above are the join roster and
    // not this roll call, but wiping first is one edit away from dropping every
    // multi-hop supporter out of the COUNTDOWN that tells them to press.
    if (eventType == ChainGameEventType::COUNTDOWN) {
        clearSupporterConfirms();
    }
}

void PosseManager::onGameEventFrame(const uint8_t* fromMac, const ChainGameEventPayload& payload) {
    // The frame is broadcast, so every chain in radio range hears it, and the
    // ack below is a unicast to its source: a frame naming our challenger but
    // sent from elsewhere would spend a peer-table slot on a stranger.
    if (!isEventFromOwnChallenger(payload.challengerMac)) return;
    if (memcmp(fromMac, payload.challengerMac, 6) != 0) return;

    // Unconditional: a COUNTDOWN voids the standing confirm whether or not
    // SupporterReady is mounted to see it.
    onChainGameEventReceived(payload.event_type);

    if (onGameEvent) onGameEvent(payload.event_type);

    // Acked even with nothing mounted to act on it, or the challenger keeps
    // retrying a WIN/LOSS this device has left behind.
    gameEventChannel.ack(fromMac, payload.seqId);
}

void PosseManager::sendConfirm() {
    // Latched before the challenger check, not after: a press that lands before
    // the role cascade has named a challenger still has to reach whoever the
    // cascade names, and that is what resendConfirm is for.
    confirmSent = true;

    const uint8_t* challenger = getChallengerMac();
    if (challenger == nullptr) return;

    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;

    ChainConfirmPayload payload{};
    memcpy(payload.originatorMac, selfMac, 6);
    // Read at send so the frame always carries the current player.
    payload.playerUuid = player->getServerPlayerId();
    chainConfirmChannel.sendReliable(challenger, payload);
}

void PosseManager::resendConfirm() {
    if (!confirmSent) return;
    sendConfirm();
}

void PosseManager::onChainGameEventReceived(uint8_t eventType) {
    if (static_cast<ChainGameEventType>(eventType) != ChainGameEventType::COUNTDOWN) return;
    confirmSent = false;
    chainConfirmChannel.cancelAll();
}

void PosseManager::onConfirmReceived(const uint8_t* originatorMac,
                                     const UUIDBytes& supporterPlayerId) {
    // Nil is tested first so a demo challenger's own nil UUID never matches a
    // supporter's.
    const bool supporterUnregistered = supporterPlayerId.isZero();
    if (supporterUnregistered || supporterPlayerId == player->getServerPlayerId()) {
        LOG_W(TAG, "confirm from %s not counted: %s", MacToString(originatorMac),
              supporterUnregistered ? "supporter not registered" : "same player as challenger");
        return;
    }
    // Whether this originator is a chain member, and whether we are the
    // challenger who gets to count it, are both read live in
    // getConfirmedSupporterCount — neither is knowable for certain at the moment
    // a press arrives.
    recordMac(receivedConfirms, receivedConfirmCount, receivedConfirmWrite, originatorMac);
}

void PosseManager::onChainJoinReceived(const uint8_t* supporterMac,
                                       const uint8_t* joinChallengerMac) {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) {
        LOG_E(TAG, "chain join dropped: no local MAC");
        return;
    }
    // A join that names another challenger reached us by radio accident; enrolling
    // its sender would hand this device a supporter from someone else's chain.
    if (memcmp(joinChallengerMac, selfMac, 6) != 0) return;
    recordMac(supporterRoster, supporterRosterCount, supporterRosterWrite, supporterMac);
}

void PosseManager::announceToChallenger() {
    const uint8_t* challenger = getChallengerMac();
    if (challenger == nullptr) return;
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;
    // A challenger is its own chain's root, not a member of it.
    if (memcmp(challenger, selfMac, 6) == 0) return;

    ChainJoinPayload payload{};
    memcpy(payload.challengerMac, challenger, 6);
    chainJoinChannel.sendReliable(challenger, payload);
}

void PosseManager::onChainStateChanged() {
    // Losing the supporter-jack cable strands the entire chain below it, however
    // deep, so both the roll call and the roster it is scored against go with it.
    size_t count = (rdc->getPeerMac(supporterJack()) != nullptr &&
                    peerIsHunter(supporterJack()) == player->isHunter())
                       ? 1u
                       : 0u;
    if (lastSupporterChainCount > 0 && count == 0) {
        clearSupporterConfirms();
        supporterRosterCount = 0;
        supporterRosterWrite = 0;
    }
    lastSupporterChainCount = count;

    if (isChallenger()) {
        const bool opponentConnecting = rdc->getPeerMac(opponentJack()) != nullptr &&
                                        !peerIsHunter(opponentJack()).has_value();
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr && (!opponentConnecting || !challengerMac.has_value())) {
            std::array<uint8_t, 6> selfArr;
            memcpy(selfArr.data(), selfMac, 6);
            challengerMac = selfArr;
        }
    } else if (isSupporter()) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (challengerMac.has_value() && selfMac != nullptr &&
            memcmp(challengerMac->data(), selfMac, 6) == 0) {
            challengerMac.reset();
        }
    } else {
        challengerMac.reset();
    }

    broadcastRoleAndChallenger();

    if (isSupporter()) {
        // Re-send on every topology event: join has no backstop once its retry span expires.
        announceToChallenger();
    } else {
        // Clear confirmSent so patching into another chain mid-round cannot forward a prior press.
        confirmSent = false;
        chainConfirmChannel.cancelAll();
        chainJoinChannel.cancelAll();
    }
}

unsigned long PosseManager::getBoostMs() const {
    return getConfirmedSupporterCount() * BOOST_PER_SUPPORTER_MS;
}

size_t PosseManager::getConfirmedSupporterCount() const {
    if (!isChallenger()) return 0;
    std::vector<std::array<uint8_t, 6>> peers = getSupporterChainPeers();
    size_t confirmed = 0;
    for (size_t i = 0; i < receivedConfirmCount; i++) {
        for (const std::array<uint8_t, 6>& peer : peers) {
            if (memcmp(peer.data(), receivedConfirms[i].data(), 6) == 0) {
                confirmed++;
                break;
            }
        }
    }
    return confirmed;
}

const uint8_t* PosseManager::getChallengerMac() const {
    if (rdc->isInRing()) return nullptr;
    return challengerMac.has_value() ? challengerMac->data() : nullptr;
}

void PosseManager::clearSupporterConfirms() {
    receivedConfirmCount = 0;
    receivedConfirmWrite = 0;
}

void PosseManager::onRoleAnnounceReceived(
    const uint8_t* fromMac,
    const uint8_t* announcedChallengerMac) {
    const uint8_t* opponentPeer = rdc->getPeerMac(opponentJack());
    if (opponentPeer == nullptr || memcmp(opponentPeer, fromMac, 6) != 0) return;
    if (rdc->isInRing() || peerIsHunter(opponentJack()) == !player->isHunter()) return;

    // Wire input: the field is always present, so a sender with no challenger sends
    // it zeroed, and a two-bounty ring does exactly that: both clear their challenger,
    // then each announces to the other's opponent jack with a matching role. Caching
    // the zero would cascade it onward as this device's challenger.
    if (MacToUInt64(announcedChallengerMac) == 0) {
        challengerMac.reset();
        chainConfirmChannel.cancelAll();
        chainJoinChannel.cancelAll();
        if (isSupporter()) broadcastRoleAndChallenger();
        return;
    }

    std::array<uint8_t, 6> newMac;
    memcpy(newMac.data(), announcedChallengerMac, 6);
    bool changed = !challengerMac.has_value() || *challengerMac != newMac;
    challengerMac = newMac;
    if (changed) {
        chainConfirmChannel.cancelAll();
        chainJoinChannel.cancelAll();
    }
    if (!isSupporter()) return;
    const bool cascadeInFlight = supporterAnnounce.has_value() &&
                                 !supporterAnnounce->delivered &&
                                 roleAnnounceChannel.isPending(supporterAnnounce->peer.data());
    if (changed || !cascadeInFlight) {
        supporterAnnounce.reset();
        broadcastRoleAndChallenger();
    }
    // Upstream link or challenger changes leave our own jacks untouched; re-enroll and re-confirm on every cascade.
    announceToChallenger();
    resendConfirm();
}

void PosseManager::recordAnnounceDelivered(uint8_t seqId, const uint8_t* mac) {
    if (mac == nullptr || !supporterAnnounce.has_value()) return;
    if (supporterAnnounce->seqId != seqId ||
        memcmp(supporterAnnounce->peer.data(), mac, 6) != 0) {
        return;
    }
    supporterAnnounce->delivered = true;
}

void PosseManager::broadcastRoleAndChallenger() {
    const uint8_t* supporterPeer = rdc->getPeerMac(supporterJack());
    const uint8_t* challenger = getChallengerMac();
    if (supporterAnnounce.has_value() &&
        (supporterPeer == nullptr || challenger == nullptr ||
         memcmp(supporterAnnounce->peer.data(), supporterPeer, 6) != 0)) {
        roleAnnounceChannel.cancel(supporterAnnounce->peer.data());
        supporterAnnounce.reset();
    }
    if (supporterPeer == nullptr || challenger == nullptr) return;

    RoleAnnounceState content;
    memcpy(content.peer.data(), supporterPeer, 6);
    memcpy(content.challenger.data(), challenger, 6);
    if (supporterAnnounce.has_value() && supporterAnnounce->told(content)) return;

    // Half-open gate, same reasoning as canInitiateMatch. A jack at Connecting has
    // a peer MAC off one inbound serial frame; the supporter has not necessarily
    // parsed ours yet, and it drops announces from a MAC it does not yet hold as a
    // direct peer. The radio would still report the frame delivered, so the retry
    // clears and the announce is simply lost. Connected means its context came back
    // over the radio, which it could only send having already recorded us.
    if (rdc->getJackStatus(supporterJack()) != JackStatus::CONNECTED) return;

    RoleAnnouncePayload payload{};
    memcpy(payload.challengerMac, content.challenger.data(), 6);

    roleAnnounceSentTimer.setTimer(0);
    content.seqId = roleAnnounceChannel.sendReliable(supporterPeer, payload);
    supporterAnnounce = content;
}

void PosseManager::sync() {
    if (roleAnnounceBackstopTimer.expired()) {
        roleAnnounceBackstopTimer.setTimer(ROLE_ANNOUNCE_BACKSTOP_MS);
        broadcastRoleAndChallenger();
    }
}
