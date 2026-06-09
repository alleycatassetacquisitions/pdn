#include "game/chain-manager.hpp"
#include <algorithm>
#include "device/drivers/logger.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/mac-functions.hpp"

#define TAG "CHAIN"

namespace chain_role {

std::optional<net::Mac> championMac(const PeerGraph& g) {
    // A closed loop has no champion (it arms a shootout). Gate on the same
    // loop-detection the coordinator election uses so the two derivations can
    // never contradict each other.
    if (g.isInLoop()) return std::nullopt;

    net::Mac cur = g.getSelfMac();
    std::vector<net::Mac> visited;
    while (true) {
        if (std::find(visited.begin(), visited.end(), cur) != visited.end()) {
            // Re-entered a node without isInLoop() catching it: treat as no
            // champion rather than spin. Defensive; isInLoop() already covers
            // closed cycles.
            return std::nullopt;
        }
        visited.push_back(cur);

        auto v = g.nodeView(cur);
        if (!v.has_value()) return std::nullopt;
        const Role role = fromByte(v->role);
        // Only reachable on the first iteration (a NONE node is never stepped
        // onto): a non-duelist self has no champion.
        if (role == Role::NONE) return std::nullopt;

        // Opponent jack: OUTPUT for a hunter, INPUT for a bounty. Walking it
        // climbs toward the head of the same-role chain.
        const net::Mac& opp =
            role == Role::HUNTER ? v->outPeer : v->inPeer;

        if (opp == net::Mac{}) return cur;             // open jack: cur is the head
        if (!g.hasMutualEdge(cur, opp)) return cur;    // half-open link: cur is the head

        auto ov = g.nodeView(opp);
        if (!ov.has_value()) return cur;               // opp beacon absent: stop here
        // Chain end: an opposite-role duelist (cur faces a duel opponent) or a
        // NONE node (a non-duelist severs the chain; never a candidate).
        if (fromByte(ov->role) != role) return cur;

        cur = opp;
    }
}

std::optional<bool> mutualOpponentIsHunter(const PeerGraph& g) {
    auto self = g.nodeView(g.getSelfMac());
    if (!self.has_value()) return std::nullopt;
    const Role selfRole = fromByte(self->role);
    if (selfRole == Role::NONE) return std::nullopt;  // a non-duelist has no opponent
    const net::Mac& opp =
        selfRole == Role::HUNTER ? self->outPeer : self->inPeer;
    if (opp == net::Mac{}) return std::nullopt;
    if (opp == g.getSelfMac()) return std::nullopt;   // loopback cable pathology
    if (!g.hasMutualEdge(g.getSelfMac(), opp)) return std::nullopt;
    auto ov = g.nodeView(opp);
    if (!ov.has_value()) return std::nullopt;
    const Role oppRole = fromByte(ov->role);
    if (oppRole == Role::NONE) return std::nullopt;   // a non-duelist is no opponent
    return oppRole == Role::HUNTER;
}

}  // namespace chain_role

ChainManager::ChainManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
    : player_(player),
      wirelessManager_(wirelessManager),
      rdc_(rdc) {
    // The topology layer floods the role byte opaquely; interpreting it is
    // game policy, so the champion walk and opponent-role query are injected
    // here rather than living in RDC.
    if (rdc_ != nullptr) {
        rdc_->setChampionResolver(&chain_role::championMac);
        rdc_->setOpponentRoleResolver(&chain_role::mutualOpponentIsHunter);
    }
}

SerialIdentifier ChainManager::opponentJack() const {
    return player_->isHunter() ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
}

SerialIdentifier ChainManager::supporterJack() const {
    return player_->isHunter() ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
}

bool ChainManager::championIsSelf(const std::array<uint8_t, 6>& champ) const {
    const uint8_t* self = wirelessManager_->getMacAddress();
    return self != nullptr && memcmp(champ.data(), self, 6) == 0;
}

std::optional<std::array<uint8_t, 6>> ChainManager::getChampionMac() const {
    return rdc_->getChampionMac();
}

void ChainManager::claimCoordinator() {
    if (isCoordinator_) return;
    isCoordinator_ = true;
    LOG_W(TAG, "claimed coordinator");
}

void ChainManager::demoteCoordinator() {
    if (!isCoordinator_) return;
    isCoordinator_ = false;
    LOG_W(TAG, "demoted from coordinator");
}

ChainManager::ChainRole ChainManager::chainRole() const {
    // A coordinator holds no champion-relative role. The walk returns nullopt
    // inside a loop anyway; this flag check covers the window before the
    // coordinator flag clears.
    if (isCoordinator_) return ChainRole::Coordinator;
    auto champ = rdc_->getChampionMac();
    if (!champ.has_value()) return ChainRole::None;
    return championIsSelf(*champ) ? ChainRole::Champion : ChainRole::Supporter;
}

bool ChainManager::isSupporter() const { return chainRole() == ChainRole::Supporter; }

bool ChainManager::isChampion() const { return chainRole() == ChainRole::Champion; }

bool ChainManager::canInitiateMatch() const {
    if (!player_->isHunter()) return false;
    // Never start a 1v1 inside a ring. Match init is a side-effecting radio send
    // invoked imperatively from Idle, not an orderable transition, so it can't
    // rely on first-match-wins to defer to the shootout like the
    // Idle->ShootoutProposal edge does; the !isInLoop() guard is restated here.
    if (!rdc_->isTopologyStable()) return false;
    if (rdc_->isInLoop()) return false;
    // A real duel needs an opposite-role peer on the opponent jack, confirmed as
    // a mutual edge by the BEACON flood (not merely a one-way HELLO claim).
    auto opponentRole = rdc_->mutualOpponentIsHunter();
    if (!opponentRole.has_value()) return false;
    return *opponentRole != player_->isHunter();
}

std::vector<std::array<uint8_t, 6>> ChainManager::getSupporterChainPeers() const {
    // Coordinator never broadcasts chain duel events (it's in shootout mode).
    if (isCoordinator_) return {};
    std::vector<std::array<uint8_t, 6>> out;
    // Direct supporter-jack peer is the next hop down the chain.
    const uint8_t* direct = rdc_->getPeerMac(supporterJack());
    if (direct != nullptr) {
        std::array<uint8_t, 6> arr;
        std::memcpy(arr.data(), direct, 6);
        out.push_back(arr);
    }
    // Multi-hop supporters that confirmed via kChainConfirm. The radio's send
    // path registers any unseen MAC on first send.
    for (const auto& sup : confirmedSupporters_) {
        bool isDirect = (direct != nullptr && memcmp(sup.data(), direct, 6) == 0);
        if (!isDirect) out.push_back(sup);
    }
    return out;
}

bool ChainManager::isKnownGameEventSender(const uint8_t* fromMac) const {
    // Only the direct opponent-jack peer (our chain parent) forwards game
    // events to us. Strangers and indirect senders are dropped.
    const uint8_t* direct = rdc_->getPeerMac(opponentJack());
    if (direct != nullptr && memcmp(direct, fromMac, 6) == 0) return true;
    // The champion may also reach us directly over ESP-NOW. Accept its sends as
    // authoritative.
    auto champ = rdc_->getChampionMac();
    if (champ.has_value() && memcmp(champ->data(), fromMac, 6) == 0) return true;
    return false;
}

void ChainManager::sendGameEventToSupporters(ChainGameEventType eventType) {
    if (!isChampion()) return;
    if (gameEventChannel_ == nullptr) return;

    // Sent reliably: a dropped COUNTDOWN leaves a supporter un-armed (shows
    // "Missed"); WIN/LOSS drop strands it on a stale screen. SupersedePerTarget
    // keeps this safe for transient events: a newer event to a supporter drops
    // any still-pending older one, so a late COUNTDOWN can't re-arm nor a late
    // DRAW disarm a duel that already moved on.
    //
    // Target the standing posse BEFORE clearing the boost tally. A multi-hop
    // supporter is reachable only via confirmedSupporters_, so clearing first
    // would drop everyone past the first hop from the COUNTDOWN.
    ChainGameEventPayload payload{};
    payload.event_type = static_cast<uint8_t>(eventType);
    gameEventChannel_->sendReliable(getSupporterChainPeers(), payload);

    if (eventType == ChainGameEventType::COUNTDOWN) {
        clearSupporterConfirms();
    }
}

bool ChainManager::sendConfirm() {
    auto champ = rdc_->getChampionMac();
    if (!champ.has_value()) {
        LOG_W(TAG, "sendConfirm SKIP: no champion");
        return false;
    }
    if (confirmChannel_ == nullptr) return false;

    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return false;

    // The champion confirms to nobody; only supporters confirm upstream.
    if (memcmp(champ->data(), selfMac, 6) == 0) return false;

    LOG_W(TAG, "sendConfirm -> %s", MacToString(champ->data()));
    ChainConfirmPayload payload{};
    memcpy(payload.originatorMac, selfMac, 6);
    confirmChannel_->sendReliable(champ->data(), payload);
    return true;
}

void ChainManager::onConfirmReceived(
    const uint8_t* fromMac,
    const uint8_t* originatorMac,
    uint8_t seqId) {
    (void)fromMac; (void)seqId;

    // Drop confirms that arrive mid-convergence; the sender's resender retries
    // until the gate opens once isTopologyStable settles.
    if (!rdc_->isTopologyStable()) {
        LOG_W(TAG, "onConfirmReceived dropped (topology unstable)");
        return;
    }

    // Membership integrity. Accept only if the originator is visible in our
    // topology: our direct supporter-jack peer (legitimate even before its
    // BEACON propagates into our graph) or a member of our connected component
    // (getChainMembers() is a BFS over mutual BEACON edges, covering multi-hop
    // supporters). Without this a device holding a stale champion could unicast
    // a confirm and inflate the boost.
    bool isMember = false;
    const uint8_t* directSupporter = rdc_->getPeerMac(supporterJack());
    if (directSupporter != nullptr &&
        memcmp(directSupporter, originatorMac, 6) == 0) {
        isMember = true;
    }
    if (!isMember) {
        isMember = macInList(originatorMac, rdc_->getChainMembers());
    }
    if (!isMember) {
        LOG_W(TAG, "onConfirmReceived dropped (originator not in topology)");
        return;
    }

    if (macInList(originatorMac, confirmedSupporters_)) {
        LOG_W(TAG, "onConfirmReceived dedupe (already confirmed)");
        return;
    }
    std::array<uint8_t, 6> macArr;
    memcpy(macArr.data(), originatorMac, 6);
    confirmedSupporters_.push_back(macArr);
    LOG_W(TAG, "onConfirmReceived ACCEPTED from=%s newCount=%u",
          MacToString(originatorMac), (unsigned)confirmedSupporters_.size());
}

void ChainManager::onChainStateChanged() {
    // Boost-clear invariant: a confirmed supporter only counts while we hold the
    // supporter-jack direct peer it confirmed through. That peer's disappearance
    // is the only locally-observable signal the chain shrank, so drain when gone.
    const bool supporterPeerPresent = (rdc_->getPeerMac(supporterJack()) != nullptr);
    if (!supporterPeerPresent && !confirmedSupporters_.empty()) {
        LOG_W(TAG, "supporter-jack peer gone, draining %u confirmed supporters",
              (unsigned)confirmedSupporters_.size());
        clearSupporterConfirms();
    }

    // A resolved supporter confirms up to its champion so it contributes boost.
    // Idempotent at the champion (dedup by originator); the 1Hz sync() backstop
    // re-sends if this one is dropped mid-convergence.
    if (isSupporter()) sendConfirm();
}

unsigned long ChainManager::getBoostMs() const {
    return confirmedSupporters_.size() * BOOST_PER_SUPPORTER_MS;
}

size_t ChainManager::getConfirmedSupporterCount() const {
    return confirmedSupporters_.size();
}

size_t ChainManager::getChainLength() const {
    // Live count of supporters chained behind us, derived from topology
    // (distinct from confirmedSupporters_, which only fills during a countdown).
    // The posse belongs to the champion; a mid-chain supporter reports zero.
    if (!isChampion()) return 0;
    return rdc_->countChainBehind(supporterJack());
}

void ChainManager::clearSupporterConfirms() {
    confirmedSupporters_.clear();
}

void ChainManager::sync() {
    // Coord-eligibility derivation runs at ~1Hz (matches the BEACON cadence).
    unsigned long now = nowMs();
    if (now >= nextMinStabilityCheckMs_) {
        nextMinStabilityCheckMs_ = now + kCoordStabilityCycleMs;
        deriveCoordinator();
    }
    if (now >= nextConfirmBackstopMs_) {
        nextConfirmBackstopMs_ = now + kConfirmBackstopMs;
        if (isSupporter()) sendConfirm();
    }
}

unsigned long ChainManager::nowMs() const {
    auto* clk = SimpleTimer::getPlatformClock();
    return clk ? clk->milliseconds() : 0;
}

void ChainManager::deriveCoordinator() {
    // Consumers of isInLoop / getChainMembers must guard on isTopologyStable,
    // else mid-convergence views flip a coord claim or surrender it prematurely.
    // Hold prior state until the graph settles.
    const bool stable = rdc_->isTopologyStable();
    const auto members = rdc_->getChainMembers();
    const bool inLoop = rdc_->isInLoop();
    if (!stable) return;

    if (members.empty() || !inLoop) {
        lastStableMin_ = std::nullopt;
        stableMinCycles_ = 0;
        if (isCoordinator_) demoteCoordinator();
        return;
    }

    std::array<uint8_t, 6> currentMin = lowestMacIn(members);

    if (lastStableMin_.has_value() && *lastStableMin_ == currentMin) {
        if (stableMinCycles_ < 1) stableMinCycles_++;
    } else {
        // Reseed: an eligibility flip is honored only after a full cycle of
        // stability on the new min.
        lastStableMin_ = currentMin;
        stableMinCycles_ = 0;
    }

    std::array<uint8_t, 6> selfMac{};
    const uint8_t* selfPtr = wirelessManager_ ? wirelessManager_->getMacAddress() : nullptr;
    if (selfPtr == nullptr) return;
    std::memcpy(selfMac.data(), selfPtr, 6);

    if (stableMinCycles_ >= 1 && currentMin == selfMac && !isCoordinator_) {
        claimCoordinator();
    } else if (currentMin != selfMac && isCoordinator_) {
        // Defer to the lower-MAC peer; the Shootout BRACKET_ENTRY tiebreaker
        // is a separate safety net.
        demoteCoordinator();
    }
}

void ChainManager::initialize(WirelessTransport* transport) {
    transport_ = transport;

    gameEventChannel_ = transport_->channel<ChainGameEventPayload>(
        PktType::kChainGameEvent,
        [](uint8_t seqId, const uint8_t* target) {
            LOG_W(TAG, "kChainGameEvent abandoned: target=%s seqId=%u",
                  MacToString(target), (unsigned)seqId);
        });
    gameEventChannel_->onReceive(
        [this](const uint8_t* fromMac, const ChainGameEventPayload& p) {
            onGameEventReceived(fromMac, p);
        });

    confirmChannel_ = transport_->channel<ChainConfirmPayload>(
        PktType::kChainConfirm,
        [](uint8_t /*seqId*/, const uint8_t* /*target*/) {});
    confirmChannel_->onReceive(
        [this](const uint8_t* fromMac, const ChainConfirmPayload& p) {
            onConfirmReceived(fromMac, p.originatorMac, p.seqId);
        });

    // A role flip changes which jack is the opponent jack and re-floods the
    // role on the next BEACON. Re-derive now so the UI reacts immediately.
    if (player_ != nullptr) {
        player_->setOnRoleChanged([this]() {
            onChainStateChanged();
        });
    }
}

void ChainManager::onGameEventReceived(const uint8_t* fromMac,
                                       const ChainGameEventPayload& p) {
    // Only accept events from supporter-chain peers. The channel already
    // auto-acked; this drop is logical, so strangers can't drive supporter UI.
    if (!isKnownGameEventSender(fromMac)) return;
    if (gameEventObserver_) gameEventObserver_(p.event_type, fromMac);
}

void ChainManager::onDirectPeerChange(SerialIdentifier port,
                                      std::optional<RemoteDeviceCoordinator::Peer> /*previous*/,
                                      std::optional<RemoteDeviceCoordinator::Peer> current) {
    LOG_W(TAG, "onDirectPeerChange port=%c curHasVal=%d",
          port == SerialIdentifier::INPUT_JACK ? 'I' : 'O',
          (int)current.has_value());
    // Direct peer dropped while coordinator: the loop just opened on our
    // cable; demote so ShootoutManager observes it and broadcasts ABORT.
    // deriveCoordinator() catches this next sync tick too, but acting now
    // keeps the ABORT path sub-second.
    if (!current.has_value() && isCoordinator_) {
        demoteCoordinator();
    }
    onChainStateChanged();
}
