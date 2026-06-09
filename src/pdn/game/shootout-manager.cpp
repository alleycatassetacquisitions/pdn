#include "game/shootout-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/mac-types.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/mac-functions.hpp"
#include "id-generator.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

#define TAG "SHT"

namespace {
void deriveShootoutMatchId(int matchIndex, char* out, size_t outSize) {
    // Deterministic ID so both duelists prime MatchManager with the same
    // value without a SEND_MATCH_ID handshake.
    snprintf(out, outSize, "%s%032d", kShootoutMatchIdPrefix, matchIndex);
}
}

ShootoutManager::ShootoutManager(Player* player,
                                 WirelessManager* wirelessManager,
                                 RemoteDeviceCoordinator* rdc,
                                 ChainManager* chainManager)
    : player_(player), wirelessManager_(wirelessManager), rdc_(rdc), chainManager_(chainManager) {}

void ShootoutManager::initialize(WirelessTransport* transport) {
    transport_ = transport;
    if (transport_ == nullptr) return;

    auto abandonToAbort = [this](uint8_t /*seqId*/, const uint8_t* /*mac*/) {
        // BRACKET_ENTRY / MATCH_START / MATCH_RESULT abandons drop tournament
        // state. Latched and processed in sync() so we don't mutate the
        // pending list from inside the Resender's callback chain.
        tournament_.abortPending = true;
    };
    auto abandonNoop = [](uint8_t /*seqId*/, const uint8_t* /*mac*/) {};

    // Stream channel: the bracket roster is split into one reliable packet per
    // slot, all sent to the same peer. Each slot must keep its own retry entry,
    // so a dropped non-final slot still retransmits instead of being superseded.
    bracketEntryChannel_ = transport_->channel<ShootoutBracketEntryPayload, ShootoutCmd>(
        PktType::kShootoutCommand, ShootoutCmd::BRACKET_ENTRY, abandonToAbort,
        Resender::SendMode::KeepDistinct);
    bracketEntryChannel_->onReceive(
        [this](const uint8_t* fromMac, const ShootoutBracketEntryPayload& p) {
            onBracketEntryReceived(fromMac, p);
        });

    matchStartChannel_ = transport_->channel<ShootoutMatchStartPayload, ShootoutCmd>(
        PktType::kShootoutCommand, ShootoutCmd::MATCH_START, abandonToAbort);
    matchStartChannel_->onReceive(
        [this](const uint8_t* /*fromMac*/, const ShootoutMatchStartPayload& p) {
            onMatchStartReceived(p.duelistA, p.duelistB, p.matchIndex, p.seqId);
        });

    matchResultChannel_ = transport_->channel<ShootoutMatchResultPayload, ShootoutCmd>(
        PktType::kShootoutCommand, ShootoutCmd::MATCH_RESULT, abandonToAbort);
    matchResultChannel_->onReceive(
        [this](const uint8_t* fromMac, const ShootoutMatchResultPayload& p) {
            onMatchResultReceived(p.winner, p.loser, p.matchIndex, p.seqId, fromMac);
        });

    tournamentEndChannel_ = transport_->channel<ShootoutTournamentEndPayload, ShootoutCmd>(
        PktType::kShootoutCommand, ShootoutCmd::TOURNAMENT_END, abandonNoop);
    tournamentEndChannel_->onReceive(
        [this](const uint8_t* /*fromMac*/, const ShootoutTournamentEndPayload& p) {
            onTournamentEndReceived(p.winner, p.seqId);
        });

    confirmChannel_ = transport_->channel<ShootoutConfirmPayload, ShootoutCmd>(
        PktType::kShootoutCommand, ShootoutCmd::CONFIRM, abandonNoop);
    confirmChannel_->onReceive(
        [this](const uint8_t* fromMac, const ShootoutConfirmPayload& p) {
            onConfirmReceived(p.mac, p.name);
            (void)fromMac;
        });

    peerLostChannel_ = transport_->channel<ShootoutPeerLostPayload, ShootoutCmd>(
        PktType::kShootoutCommand, ShootoutCmd::PEER_LOST, abandonNoop);
    peerLostChannel_->onReceive(
        [this](const uint8_t* /*fromMac*/, const ShootoutPeerLostPayload& p) {
            onPeerLostReceived(p.mac);
        });

    abortChannel_ = transport_->channel<ShootoutAbortPayload, ShootoutCmd>(
        PktType::kShootoutCommand, ShootoutCmd::ABORT, abandonNoop);
    abortChannel_->onReceive(
        [this](const uint8_t* /*fromMac*/, const ShootoutAbortPayload& /*p*/) {
            onAbortReceived();
        });
}

bool ShootoutManager::batchIsNewer(uint8_t a, uint8_t b) {
    int8_t diff = static_cast<int8_t>(a - b);
    return diff > 0;
}

void ShootoutManager::onBracketEntryReceived(const uint8_t* fromMac,
                                             const ShootoutBracketEntryPayload& p) {
    // Competing-coordinator tiebreaker. Two rings that merge can each carry a
    // self-elected coordinator that already broadcast a bracket. Lower-MAC wins;
    // a higher-MAC coordinator that receives the lower one's BRACKET_ENTRY stands
    // down AND adopts it. Demoting ChainManager alone isn't enough once the coordinator
    // anchor points at self (isCoordinator() reads the anchor over ChainManager), so drop
    // the anchor, our bracket, and any already-underway match progress so the
    // adoption path treats us as a follower starting clean.
    if (fromMac != nullptr && isCoordinator()) {
        const uint8_t* self = wirelessManager_ ? wirelessManager_->getMacAddress() : nullptr;
        if (self != nullptr && memcmp(fromMac, self, 6) < 0) {
            if (chainManager_ != nullptr) chainManager_->demoteCoordinator();
            // Survivors: we stay in the tournament, so keep the phase, the
            // CONFIRM roster and names (the adopted bracket arrives via the
            // path below), and any latched abort.
            TournamentState fresh;
            fresh.phase = tournament_.phase;
            fresh.confirmedSet = std::move(tournament_.confirmedSet);
            fresh.names = std::move(tournament_.names);
            fresh.abortPending = tournament_.abortPending;
            tournament_ = std::move(fresh);
        }
    }
    if (isCoordinator()) return;

    // totalSlots == 0 sentinel: clear local bracket view.
    if (p.totalSlots == 0) {
        tournament_.pendingBracket.active = false;
        tournament_.bracket.clear();
        tournament_.currentRound.clear();
        return;
    }

    auto& pend = tournament_.pendingBracket;
    if (!pend.active || batchIsNewer(p.batchId, pend.batchId)) {
        pend.active = true;
        pend.batchId = p.batchId;
        pend.totalSlots = p.totalSlots;
        pend.buffer.assign(p.totalSlots, std::array<uint8_t, 6>{});
        pend.receivedSlots.assign(p.totalSlots, false);
    } else if (p.batchId != pend.batchId) {
        return; // stale batch; ignore.
    }

    if (p.slot >= pend.totalSlots) return;
    std::memcpy(pend.buffer[p.slot].data(), p.mac, 6);
    pend.receivedSlots[p.slot] = true;

    for (bool got : pend.receivedSlots) {
        if (!got) return;
    }

    tournament_.bracket = std::move(pend.buffer);
    tournament_.currentRound = tournament_.bracket;
    // The coordinator is whoever sent the BRACKET_ENTRY. Anchor here so the
    // post-bracket isCoordinator() check stays stable across cable nudges.
    if (fromMac != nullptr) {
        std::memcpy(tournament_.coordinatorMac.data(), fromMac, 6);
    } else {
        tournament_.coordinatorMac = lowestMacIn(tournament_.bracket);
    }
    pend.active = false;
    tournament_.phase = Phase::BRACKET_REVEAL;
    tournament_.bracketRevealTimer.setTimer(kBracketRevealMs);
}

bool ShootoutManager::active() const {
    return tournament_.phase != Phase::IDLE;
}

ShootoutManager::Phase ShootoutManager::getPhase() const {
    return tournament_.phase;
}

size_t ShootoutManager::getConfirmedCount() const {
    // Post-assembly the bracket is the authoritative roster; a follower's
    // confirm set only ever holds self (it unicasts CONFIRM to the
    // coordinator), so the bracket is the only place its peers appear.
    if (!tournament_.bracket.empty()) return tournament_.bracket.size();
    return tournament_.confirmedSet.size();
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::getBracket() const {
    return tournament_.bracket;
}

bool ShootoutManager::hasBye() const {
    return tournament_.bracket.size() % 2 == 1;
}


ReliableChannelBase* ShootoutManager::channelFor(ShootoutCmd cmd) const {
    switch (cmd) {
        case ShootoutCmd::BRACKET_ENTRY:  return bracketEntryChannel_;
        case ShootoutCmd::MATCH_START:    return matchStartChannel_;
        case ShootoutCmd::MATCH_RESULT:   return matchResultChannel_;
        case ShootoutCmd::TOURNAMENT_END: return tournamentEndChannel_;
        case ShootoutCmd::ABORT:          return abortChannel_;
        default:                          return nullptr;
    }
}

// Every reliable channel fans out to the bracket roster, which is the
// authoritative member set on every device once assembled.
size_t ShootoutManager::pendingAckCount(ShootoutCmd cmd) const {
    ReliableChannelBase* ch = channelFor(cmd);
    if (ch == nullptr) return 0;
    return ch->pendingCount(tournament_.bracket);
}

int ShootoutManager::getCurrentMatchIndex() const {
    return tournament_.currentMatchIndex;
}

bool ShootoutManager::isLocalDuelist() const {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return false;
    return memcmp(selfMac, tournament_.currentDuelistA.data(), 6) == 0 ||
           memcmp(selfMac, tournament_.currentDuelistB.data(), 6) == 0;
}

std::array<uint8_t, 6> ShootoutManager::getOpponentMac() const {
    return tournament_.opponentMac;
}

std::array<uint8_t, 6> ShootoutManager::getTournamentWinner() const {
    return tournament_.tournamentWinner;
}

void ShootoutManager::setLoopMembersForTest(const std::vector<std::array<uint8_t, 6>>& members) {
    testLoopMembers_ = members;
    testLoopMembersOverride_ = !members.empty();

    // Tests that need non-coordinator behavior pick a self-mac that isn't the
    // lowest, which leaves the ChainManager coordinator flag clear.
    if (chainManager_ != nullptr && wirelessManager_ != nullptr && !members.empty()) {
        const uint8_t* selfMac = wirelessManager_->getMacAddress();
        if (selfMac != nullptr) {
            auto lowest = lowestMacIn(members);
            if (memcmp(lowest.data(), selfMac, 6) == 0) {
                chainManager_->claimCoordinator();
            }
        }
    }
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::getLoopMembers() const {
    if (testLoopMembersOverride_) return testLoopMembers_;
    return buildLoopMemberSet();
}

void ShootoutManager::resetToIdle() {
    LOG_W(TAG, "resetToIdle from phase=%d", static_cast<int>(tournament_.phase));
    // Pending entries are keyed by (PktType, subType, target); iterate before
    // the reset below clears bracket/confirmedSet.
    auto cancelEachTarget = [this](auto* channel) {
        if (channel == nullptr) return;
        for (const auto& t : tournament_.bracket) channel->cancel(t.data());
        for (const auto& t : tournament_.confirmedSet) channel->cancel(t.data());
    };
    cancelEachTarget(bracketEntryChannel_);
    cancelEachTarget(matchStartChannel_);
    cancelEachTarget(matchResultChannel_);
    cancelEachTarget(tournamentEndChannel_);
    // abortChannel_ is deliberately not swept: abortTournament broadcasts a
    // reliable ABORT immediately before calling this, and those retries must
    // survive the reset or the promotion to reliable is a no-op.
    tournament_ = {};
}

void ShootoutManager::startProposal() {
    LOG_W(TAG, "startProposal");
    resetToIdle();
    tournament_.phase = Phase::PROPOSAL;
}

void ShootoutManager::confirmLocal() {
    // Gate on PROPOSAL: stale ShootoutProposal button callbacks can fire in
    // later phases and re-advance the bracket if not guarded.
    if (tournament_.phase != Phase::PROPOSAL) {
        LOG_W(TAG, "confirmLocal ignored; phase=%d", static_cast<int>(tournament_.phase));
        return;
    }
    LOG_W(TAG, "confirmLocal; confirmedCount before=%zu", tournament_.confirmedSet.size());
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return;
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), selfMac, 6);
    if (!hasConfirmed(mac.data())) {
        tournament_.confirmedSet.push_back(mac);
    }
    if (player_ != nullptr) {
        recordName(selfMac, player_->getName().c_str());
    }
    sendLocalConfirm();
    if (isCoordinator() && allMembersConfirmed()) {
        LOG_W(TAG, "allMembersConfirmed -> advanceToBracketReveal");
        advanceToBracketReveal();
    }
}

void ShootoutManager::onConfirmReceived(const uint8_t* fromMac, const char* name) {
    // A non-coordinator learns the shootout is starting from the first inbound
    // CONFIRM. Promote IDLE->PROPOSAL so the gates below accept it and downstream
    // transitions can fire (SupporterReady->Idle->ShootoutProposal).
    if (tournament_.phase == Phase::IDLE && !isCoordinator()) {
        tournament_.phase = Phase::PROPOSAL;
    }
    if (tournament_.phase != Phase::PROPOSAL) return;
    const bool firstSeen = !hasConfirmed(fromMac);

    // Coordinator gates first-time CONFIRMs by loop-membership (getLoopMembers(),
    // derived from settled topology), not by who confirmed. Non-coordinators
    // lack a stable view (they learn the roster via BRACKET_ENTRY) so accept
    // every CONFIRM until then.
    if (firstSeen && isCoordinator()) {
        if (!macInList(fromMac, getLoopMembers())) return;
    }
    recordName(fromMac, name);
    if (firstSeen) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), fromMac, 6);
        tournament_.confirmedSet.push_back(mac);
        LOG_W(TAG, "onConfirmReceived from=%s count=%zu",
              MacToString(fromMac), tournament_.confirmedSet.size());
    }

    // Only the coordinator's tally gates the tournament. Non-coordinators advance
    // on BRACKET_ENTRY, not a local count: leaving PROPOSAL on its own tally
    // would stop it helping the coordinator finish its gather.
    if (isCoordinator() && allMembersConfirmed()) {
        LOG_W(TAG, "allMembersConfirmed -> advanceToBracketReveal");
        advanceToBracketReveal();
    }
}

void ShootoutManager::recordName(const uint8_t* mac, const char* name) {
    if (name == nullptr) return;
    char buf[kNameLength + 1];
    strncpy(buf, name, kNameLength);
    buf[kNameLength] = '\0';
    if (buf[0] == '\0') return;
    for (auto& entry : tournament_.names) {
        if (memcmp(entry.mac.data(), mac, 6) == 0) {
            entry.name = buf;
            return;
        }
    }
    NameEntry e;
    memcpy(e.mac.data(), mac, 6);
    e.name = buf;
    tournament_.names.push_back(std::move(e));
}

std::string ShootoutManager::getNameForMac(const uint8_t* mac) const {
    for (const auto& entry : tournament_.names) {
        if (memcmp(entry.mac.data(), mac, 6) == 0) return entry.name;
    }
    char fallback[4];
    snprintf(fallback, sizeof(fallback), "%02X", mac[5]);
    return fallback;
}

bool ShootoutManager::hasConfirmed(const uint8_t* mac) const {
    return macInList(mac, tournament_.confirmedSet);
}

bool ShootoutManager::allMembersConfirmed() const {
    auto members = getLoopMembers();
    if (members.empty()) return false;
    for (const auto& m : members) {
        if (!hasConfirmed(m.data())) return false;
    }
    return true;
}

std::array<uint8_t, 6> ShootoutManager::getCoordinatorMac() const {
    if (!tournament_.bracket.empty()) return tournament_.coordinatorMac;
    return lowestMacIn(tournament_.confirmedSet);
}

bool ShootoutManager::isCoordinator() const {
    // Pre-bracket: defer to ChainManager. Post-bracket: anchor on the coordinator MAC (set
    // when the bracket was generated locally or adopted via BRACKET_ENTRY). A
    // mid-tournament ChainManager demote (silent-link timeout, cable nudge) must NOT change
    // who runs the tournament; the coordinator stays put until ABORT/END.
    const uint8_t* selfMac = wirelessManager_ ? wirelessManager_->getMacAddress() : nullptr;
    if (selfMac == nullptr) return false;
    if (!net::isAllZeroMac(tournament_.coordinatorMac)) {
        return memcmp(tournament_.coordinatorMac.data(), selfMac, 6) == 0;
    }
    return chainManager_ != nullptr && chainManager_->isCoordinator();
}

void ShootoutManager::generateBracket() {
    tournament_.bracket = tournament_.confirmedSet;
    // Whoever builds the bracket is the coordinator: self.
    if (wirelessManager_ != nullptr && wirelessManager_->getMacAddress() != nullptr) {
        std::memcpy(tournament_.coordinatorMac.data(), wirelessManager_->getMacAddress(), 6);
    } else {
        tournament_.coordinatorMac = lowestMacIn(tournament_.bracket);
    }
    // std::random_device is deterministic under newlib on ESP32, so seed
    // from platform clock XOR self-MAC to get real variation.
    unsigned long seed = 0;
    auto* clk = SimpleTimer::getPlatformClock();
    if (clk != nullptr) seed = clk->milliseconds();
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac != nullptr) {
        for (int i = 0; i < 6; i++) {
            seed ^= static_cast<unsigned long>(selfMac[i]) << ((i % 4) * 8);
        }
    }
    std::mt19937 rng(seed);
    std::shuffle(tournament_.bracket.begin(), tournament_.bracket.end(), rng);
    tournament_.currentRound = tournament_.bracket;
}

void ShootoutManager::primeMatchManagerForMatch() {
    if (!matchManager_) return;
    if (!isLocalDuelist()) return;

    // Role-for-this-match from MAC ordering: both sides compute the same
    // ordering so the hunter_draw_time and bounty_time slots in MatchManager
    // are written by exactly one duelist each.
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    bool localIsHunterForMatch = selfMac != nullptr &&
        memcmp(selfMac, tournament_.opponentMac.data(), 6) < 0;

    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    deriveShootoutMatchId(tournament_.currentMatchIndex, matchId, sizeof(matchId));
    LOG_W(TAG, "primeMatchManagerForMatch matchIndex=%d localHunter=%d",
          tournament_.currentMatchIndex, localIsHunterForMatch);
    // Pass the MAC-ordered slot to the match; do NOT touch the global
    // hunter/bounty role (Player::isHunter).
    matchManager_->initializeShootoutMatch(matchId, tournament_.opponentMac.data(), localIsHunterForMatch);
}

void ShootoutManager::advanceToBracketReveal() {
    tournament_.phase = Phase::BRACKET_REVEAL;
    tournament_.bracketRevealTimer.setTimer(kBracketRevealMs);
    if (isCoordinator()) {
        generateBracket();
        sendBracketToPeers();
    }
}

void ShootoutManager::sendBracketToPeers() {
    if (tournament_.bracket.empty() || bracketEntryChannel_ == nullptr) return;
    if (tournament_.bracket.size() > MAX_SHOOTOUT_MEMBERS) {
        LOG_E(TAG, "bracket size %zu exceeds ESP-NOW peer cap (%u); aborting",
              tournament_.bracket.size(), (unsigned)MAX_SHOOTOUT_MEMBERS);
        abortTournament();
        return;
    }
    uint8_t batchId = nextBracketBatchId_++;
    if (nextBracketBatchId_ == 0) nextBracketBatchId_ = 1;
    uint8_t total = static_cast<uint8_t>(tournament_.bracket.size());
    const uint8_t* selfMac = wirelessManager_->getMacAddress();

    for (const auto& target : tournament_.bracket) {
        if (selfMac && std::memcmp(target.data(), selfMac, 6) == 0) continue;
        // The receiver dedups by batchId and discards prior-batch slots, so
        // retransmitting them is wasted airtime that piles up per-slot on a
        // flaky link (each slot is its own KeepDistinct seqId).
        bracketEntryChannel_->cancel(target.data());
        for (uint8_t i = 0; i < total; ++i) {
            ShootoutBracketEntryPayload p{};
            p.batchId = batchId;
            p.slot = i;
            p.totalSlots = total;
            std::memcpy(p.mac, tournament_.bracket[i].data(), 6);
            bracketEntryChannel_->sendReliable(target.data(), p);
        }
    }
}

void ShootoutManager::cancelPendingForTest(ShootoutCmd cmd, const uint8_t* fromMac) {
    // The single-device test fixture has no peer to ack back, so tests cancel
    // by MAC to mean "this peer is caught up".
    ReliableChannelBase* ch = channelFor(cmd);
    if (ch != nullptr) ch->cancel(fromMac);
}

void ShootoutManager::abortTournament() {
    // ENDED is terminal-success; this guard stops a straggling
    // MATCH_RESULT abandon from latching tournament_.abortPending
    // and demoting the success state.
    if (tournament_.phase == Phase::ABORTED || tournament_.phase == Phase::ENDED) return;
    LOG_W(TAG, "abortTournament from phase=%d", static_cast<int>(tournament_.phase));

    // Broadcast before resetToIdle clears the bracket and confirm rosters.
    // Reliable, like the other tournament-terminal commands: a peer that
    // misses ABORT sits in a dead tournament until disconnect detection or
    // its own retry exhaustion bails it out. resetToIdle below deliberately
    // leaves abortChannel_ out of its cancel sweep so these retries stay
    // armed; an unreachable peer's slot abandons silently (noop callback).
    if (abortChannel_ != nullptr) {
        abortChannel_->sendReliable(currentRoster(), ShootoutAbortPayload{});
    }

    resetToIdle();
    tournament_.phase = Phase::ABORTED;
}

void ShootoutManager::sendLocalConfirm() {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return;

    ShootoutConfirmPayload p{};
    memcpy(p.mac, selfMac, 6);
    memset(p.name, 0, kNameLength);
    if (player_ != nullptr) {
        const std::string& n = player_->getName();
        size_t copyLen = n.size() < kNameLength ? n.size() : kNameLength;
        memcpy(p.name, n.data(), copyLen);
    }

    if (confirmChannel_ == nullptr) return;

    // Reliable: the Resender retransmits a dropped CONFIRM regardless of phase.
    // The coordinator addresses every loop member; a non-coordinator addresses
    // the coordinator (lowest MAC in the settled loop). The ESP-NOW driver
    // auto-registers a peer on first send, so it need not be a jack neighbour.
    if (isCoordinator()) {
        confirmChannel_->sendReliable(getLoopMembers(), p);
    } else {
        auto members = getLoopMembers();
        if (!members.empty()) {
            auto coord = lowestMacIn(members);
            confirmChannel_->sendReliable(coord.data(), p);
        } else {
            for (auto jack : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
                const uint8_t* direct = rdc_->getPeerMac(jack);
                if (direct != nullptr) confirmChannel_->sendReliable(direct, p);
            }
        }
    }
}

void ShootoutManager::sync() {
    // Resender retransmits are pumped by the platform loop, not here.
    if (tournament_.abortPending) {
        tournament_.abortPending = false;
        abortTournament();
    }

    maybeStartNextMatch();
}

std::pair<std::array<uint8_t,6>, std::array<uint8_t,6>>
ShootoutManager::getCurrentMatchPair() const {
    if (tournament_.currentMatchIndex < 0) return {};
    return {tournament_.currentDuelistA, tournament_.currentDuelistB};
}

void ShootoutManager::sendMatchStartToPeers(int matchIndex) {
    const auto& a = tournament_.currentRound[matchIndex * 2];
    const auto& b = tournament_.currentRound[matchIndex * 2 + 1];
    adoptCurrentMatch(a.data(), b.data(), matchIndex);

    if (matchStartChannel_ == nullptr) return;
    ShootoutMatchStartPayload p{};
    std::memcpy(p.duelistA, a.data(), 6);
    std::memcpy(p.duelistB, b.data(), 6);
    p.matchIndex = static_cast<uint8_t>(matchIndex);

    matchStartChannel_->sendReliable(tournament_.bracket, p);
}

bool ShootoutManager::adoptCurrentMatch(const uint8_t* a, const uint8_t* b,
                                        int matchIndex) {
    if (isSameMatch(matchIndex, a, b)) return false;
    memcpy(tournament_.currentDuelistA.data(), a, 6);
    memcpy(tournament_.currentDuelistB.data(), b, 6);
    tournament_.currentMatchIndex = matchIndex;
    tournament_.reportedLocalWin = false;
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, a, 6) == 0) ? b : a;
        memcpy(tournament_.opponentMac.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    return true;
}


bool ShootoutManager::isSameMatch(int matchIndex, const uint8_t* a, const uint8_t* b) const {
    return matchIndex == tournament_.currentMatchIndex
        && tournament_.phase == Phase::MATCH_IN_PROGRESS
        && memcmp(tournament_.currentDuelistA.data(), a, 6) == 0
        && memcmp(tournament_.currentDuelistB.data(), b, 6) == 0;
}

bool ShootoutManager::isActiveDuelist(const uint8_t* mac) const {
    if (tournament_.currentMatchIndex < 0) return false;
    auto pair = getCurrentMatchPair();
    return memcmp(pair.first.data(), mac, 6) == 0 ||
           memcmp(pair.second.data(), mac, 6) == 0;
}


void ShootoutManager::onDirectPeerChange(SerialIdentifier /*port*/,
                                         std::optional<RemoteDeviceCoordinator::Peer> previous,
                                         std::optional<RemoteDeviceCoordinator::Peer> current) {
    // Connect transitions don't change tournament state; the peer must have
    // already participated in CONFIRM/BRACKET to be a duelist.
    if (current.has_value() || !previous.has_value()) return;
    onLocalRDCDisconnect(previous->mac.data());
}

void ShootoutManager::onLocalRDCDisconnect(const uint8_t* lostMac) {
    LOG_W(TAG, "onLocalRDCDisconnect %s phase=%d",
          MacToString(lostMac), static_cast<int>(tournament_.phase));
    if (tournament_.phase == Phase::IDLE || tournament_.phase == Phase::ABORTED || tournament_.phase == Phase::ENDED) return;
    // Stale-disconnect filter: if the "lost" peer is still our RDC direct peer
    // (the jack's HELLO link is still alive), don't fire PEER_LOST. Transitive
    // reachability isn't tracked here; bracket-side filtering happens via the
    // coordinator's ABORT-on-demotion path.
    if (rdc_->isDirectPeer(lostMac)) return;
    if (peerLostChannel_ != nullptr) {
        ShootoutPeerLostPayload p{};
        memcpy(p.mac, lostMac, 6);
        peerLostChannel_->sendOnce(currentRoster(), p);
    }
    onPeerLostReceived(lostMac);
}

void ShootoutManager::onPeerLostReceived(const uint8_t* lostMac) {
    LOG_W(TAG, "onPeerLostReceived %s phase=%d",
          MacToString(lostMac), static_cast<int>(tournament_.phase));
    if (tournament_.phase == Phase::IDLE || tournament_.phase == Phase::ABORTED || tournament_.phase == Phase::ENDED) return;
    // If the "lost" peer is still our direct RDC peer the message is stale
    // for us (we'd observe the loss locally first).
    if (rdc_->isDirectPeer(lostMac)) return;
    abortTournament();
}

void ShootoutManager::maybeStartNextMatch() {
    if (!isCoordinator()) return;
    if (tournament_.phase != Phase::BRACKET_REVEAL && tournament_.phase != Phase::BETWEEN_MATCHES) return;
    if (tournament_.phase == Phase::BRACKET_REVEAL && !tournament_.bracketRevealTimer.expired()) return;
    // Cheapest gates first: this runs every sync() tick on the coordinator,
    // and the ack scan is linear in bracket × pending entries.
    if (pendingAckCount(ShootoutCmd::BRACKET_ENTRY) != 0) return;
    // Re-entrancy guard: reportLocalWin calls maybeStartNextMatch directly and
    // the same sync() tick can re-enter via the match-advance path. Both run on
    // the main loop, so this guards same-tick recursion, not concurrency.
    if (inMaybeStartNextMatch_) return;
    inMaybeStartNextMatch_ = true;
    struct Guard { bool& f; ~Guard() { f = false; } } guard{inMaybeStartNextMatch_};

    tournament_.currentMatchIndex++;
    int pairEnd = tournament_.currentMatchIndex * 2 + 1;
    if (pairEnd >= (int)tournament_.currentRound.size()) {
        std::vector<std::array<uint8_t, 6>> survivors;
        for (const auto& m : tournament_.currentRound) {
            if (!isEliminated(m.data())) survivors.push_back(m);
        }
        if (survivors.size() <= 1) {
            auto winner = survivors.empty()
                ? findLastRemaining()
                : survivors[0];
            sendTournamentEndToPeers(winner.data());
            return;
        }
        LOG_W(TAG, "advancing round: %zu survivors -> %zu",
              tournament_.currentRound.size(), survivors.size());
        tournament_.currentRound = survivors;
        tournament_.currentMatchIndex = 0;
    }
    sendMatchStartToPeers(tournament_.currentMatchIndex);
    tournament_.phase = Phase::MATCH_IN_PROGRESS;
}

void ShootoutManager::onMatchStartReceived(
    const uint8_t* duelistA, const uint8_t* duelistB,
    uint8_t matchIndex, uint8_t seqId) {
    // The coordinator drives its own matches, so it ignores inbound MATCH_START.
    if (isCoordinator()) return;
    (void)seqId;  // immediate-duplicate dedup is the channel's job; dedup by match below
    if (!adoptCurrentMatch(duelistA, duelistB, matchIndex)) return;
    tournament_.phase = Phase::MATCH_IN_PROGRESS;
}

bool ShootoutManager::isEliminated(const uint8_t* mac) const {
    return macInList(mac, tournament_.eliminated);
}

void ShootoutManager::applyMatchResult(const uint8_t* winner, const uint8_t* loser) {
    if (!isEliminated(loser)) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), loser, 6);
        tournament_.eliminated.push_back(mac);
    }
    tournament_.phase = Phase::BETWEEN_MATCHES;
}

void ShootoutManager::sendMatchResultToPeers(
    const uint8_t* winner, const uint8_t* loser, uint8_t matchIndex) {
    if (matchResultChannel_ == nullptr) return;
    ShootoutMatchResultPayload p{};
    std::memcpy(p.winner, winner, 6);
    std::memcpy(p.loser, loser, 6);
    p.matchIndex = matchIndex;

    matchResultChannel_->sendReliable(tournament_.bracket, p);
}

void ShootoutManager::reportLocalWin() {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return;
    if (tournament_.reportedLocalWin) return;
    tournament_.reportedLocalWin = true;
    LOG_W(TAG, "reportLocalWin matchIndex=%d", tournament_.currentMatchIndex);
    sendMatchResultToPeers(selfMac, tournament_.opponentMac.data(), static_cast<uint8_t>(tournament_.currentMatchIndex));
    applyMatchResult(selfMac, tournament_.opponentMac.data());
    if (isCoordinator()) maybeStartNextMatch();
}

void ShootoutManager::onMatchResultReceived(
    const uint8_t* winner, const uint8_t* loser,
    uint8_t matchIndex, uint8_t seqId, const uint8_t* fromMac) {
    (void)seqId; (void)fromMac;
    // Dedup by loser-MAC rather than seqId: non-coord senders have independent
    // seq counters, but each loser is eliminated exactly once per tournament.
    if (isEliminated(loser)) {
        return;
    }
    LOG_W(TAG, "onMatchResultReceived matchIndex=%u", matchIndex);
    applyMatchResult(winner, loser);
    if (isCoordinator()) maybeStartNextMatch();
}


std::array<uint8_t, 6> ShootoutManager::findLastRemaining() const {
    for (const auto& m : tournament_.bracket) {
        if (!isEliminated(m.data())) return m;
    }
    return {};
}

void ShootoutManager::sendTournamentEndToPeers(const uint8_t* winner) {
    LOG_W(TAG, "tournamentEnd winner=%s", MacToString(winner));
    if (tournamentEndChannel_ != nullptr) {
        ShootoutTournamentEndPayload p{};
        std::memcpy(p.winner, winner, 6);
        // Fan out to the whole bracket, eliminated players included, or they
        // stall in BETWEEN_MATCHES without the tournament-end transition.
        tournamentEndChannel_->sendReliable(tournament_.bracket, p);
    }
    enterEnded(winner);
}

void ShootoutManager::onTournamentEndReceived(const uint8_t* winner, uint8_t seqId) {
    (void)seqId;  // immediate-duplicate dedup is the channel's job; re-run is idempotent
    enterEnded(winner);
}

void ShootoutManager::enterEnded(const uint8_t* winner) {
    memcpy(tournament_.tournamentWinner.data(), winner, 6);
    tournament_.phase = Phase::ENDED;
    // Drop any in-flight MATCH_RESULT pending so a straggling abandon does
    // not latch tournament_.abortPending after the tournament has ended.
    if (matchResultChannel_ != nullptr) {
        matchResultChannel_->cancel(tournament_.bracket);
    }
}

void ShootoutManager::onAbortReceived() {
    // ENDED is terminal-success and must not be demoted by a stray ABORT
    // from a peer that diverged.
    if (tournament_.phase == Phase::ABORTED || tournament_.phase == Phase::IDLE || tournament_.phase == Phase::ENDED) return;
    resetToIdle();
    tournament_.phase = Phase::ABORTED;
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::buildLoopMemberSet() const {
    // Building a bracket from mid-convergence partial topology state produces
    // inconsistent members across devices. Wait for the peer-graph topology to
    // settle (isTopologyStable) before opening any derived state from it.
    if (!rdc_->isTopologyStable()) return {};
    return rdc_->getChainMembers();
}
