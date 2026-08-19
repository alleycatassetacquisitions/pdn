#include "game/shootout-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/mac-functions.hpp"
#include "id-generator.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

#define TAG "SHT"

namespace {
// Indexed by retry count. Table length matches kMaxShootoutAckRetries.
constexpr unsigned long kAckBackoffMs[] = {100, 200, 400};

void deriveShootoutMatchId(int matchIndex, char* out, size_t outSize) {
    // Deterministic ID so both duelists prime MatchManager with the same
    // value without a SEND_MATCH_ID handshake.
    snprintf(out, outSize, "%s%032d", kShootoutMatchIdPrefix, matchIndex);
}
}

ShootoutManager::ShootoutManager(Player* player,
                                 WirelessManager* wirelessManager,
                                 RemoteDeviceCoordinator* rdc)
    : player(player)
    , wirelessManager(wirelessManager)
    , rdc(rdc) {
    if (rdc == nullptr) return;
    // Subscribed here rather than by whoever builds this manager: see
    // ChainDuelManager's constructor for the reasoning.
    rdc->setPeerLostCallback([this](const uint8_t* lostMac) { onLocalRDCDisconnect(lostMac); });
    rdc->setOnRingClosed([this]() { onRingClosed(); });
}

ShootoutManager::~ShootoutManager() {
    if (rdc == nullptr) return;
    rdc->setPeerLostCallback(nullptr);
    rdc->setOnRingClosed(nullptr);
}

bool ShootoutManager::active() const {
    return phase != Phase::IDLE;
}

ShootoutManager::Phase ShootoutManager::getPhase() const {
    return phase;
}

size_t ShootoutManager::getConfirmedCount() const {
    return confirmedSet.size();
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::getBracket() const {
    return bracket;
}

bool ShootoutManager::hasBye() const {
    return bracket.size() % 2 == 1;
}

uint8_t ShootoutManager::getLastBracketSeqId() const {
    return lastBracketSeqId;
}

size_t ShootoutManager::getBracketPendingAckCount() const {
    return bracketPendingAcks.size();
}

int ShootoutManager::getCurrentMatchIndex() const {
    return currentMatchIndex;
}

bool ShootoutManager::isLocalDuelist() const {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return false;
    return memcmp(selfMac, currentDuelistA.data(), 6) == 0 ||
           memcmp(selfMac, currentDuelistB.data(), 6) == 0;
}

uint8_t ShootoutManager::nextSeqId() {
    uint8_t id = nextShootoutSeqId++;
    if (nextShootoutSeqId == 0) nextShootoutSeqId = 1;
    return id;
}

bool ShootoutManager::containsMac(const std::vector<std::array<uint8_t, 6>>& set,
                                  const uint8_t* mac) {
    if (mac == nullptr) return false;
    for (const std::array<uint8_t, 6>& entry : set) {
        if (memcmp(entry.data(), mac, 6) == 0) return true;
    }
    return false;
}

bool ShootoutManager::isRingMember(const uint8_t* mac) const {
    if (containsMac(bracket, mac)) return true;
    if (containsMac(confirmedSet, mac)) return true;
    return containsMac(getLoopMembers(), mac);
}

void ShootoutManager::broadcastCommand(const uint8_t* packet, size_t len) {
    wirelessManager->sendEspNowData(wirelessManager->getBroadcastAddress(),
                                    PktType::kShootoutCommand, packet, len);
}

void ShootoutManager::broadcastToRing(const std::vector<std::array<uint8_t, 6>>& peers,
                                      const uint8_t* packet, size_t len) {
    // A ring fan-out is one broadcast frame, not one unicast per member: the
    // ESP-NOW peer table holds 20 entries, so unicast addressing cannot reach a
    // ring larger than that at all, whereas the broadcast slot is registered once
    // at radio init. Receivers must drop commands naming MACs outside their own ring.
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (!std::any_of(peers.begin(), peers.end(), [selfMac](const std::array<uint8_t, 6>& m) {
            return selfMac == nullptr || memcmp(m.data(), selfMac, 6) != 0;
        })) {
        return;
    }
    broadcastCommand(packet, len);
}

void ShootoutManager::sendReliablyToPeers(std::vector<BracketPending>& pending,
                                          const std::vector<std::array<uint8_t, 6>>& peers,
                                          const uint8_t* packet, size_t len) {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    pending.clear();
    for (const std::array<uint8_t, 6>& m : peers) {
        if (selfMac != nullptr && memcmp(m.data(), selfMac, 6) == 0) continue;
        BracketPending p;
        p.peer = m;
        p.timer.setTimer(ackTimeoutForRetry(0));
        pending.push_back(p);
    }
    if (!pending.empty()) broadcastCommand(packet, len);
}

bool ShootoutManager::retryPendingRound(std::vector<BracketPending>& pending,
                                        const uint8_t* packet, size_t len) {
    if (std::none_of(pending.begin(), pending.end(), [](BracketPending& p) { return p.timer.expired(); })) return false;

    bool exhausted = false;
    for (auto it = pending.begin(); it != pending.end();) {
        if (it->retries >= kMaxShootoutAckRetries) {
            LOG_W(TAG, "shootout ack retries exhausted for %s", MacToString(it->peer.data()));
            it = pending.erase(it);
            exhausted = true;
        } else {
            ++it;
        }
    }
    if (pending.empty()) return exhausted;

    // One frame covers every member still owing an ack; a per-peer unicast
    // retry would need a peer-table slot each.
    broadcastCommand(packet, len);
    for (BracketPending& p : pending) {
        p.retries++;
        p.timer.setTimer(ackTimeoutForRetry(p.retries));
    }
    return exhausted;
}

void ShootoutManager::eraseFromPending(std::vector<BracketPending>& pending,
                                       const uint8_t* fromMac) {
    for (auto it = pending.begin(); it != pending.end(); ) {
        if (memcmp(it->peer.data(), fromMac, 6) == 0) {
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
}

std::array<uint8_t, 6> ShootoutManager::getOpponentMac() const {
    return opponentMac;
}

uint8_t ShootoutManager::getLastMatchStartSeqId() const {
    return lastMatchStartSeqId;
}

size_t ShootoutManager::getTournamentEndPendingAckCount() const {
    return tournamentEndPendingAcks.size();
}

std::array<uint8_t, 6> ShootoutManager::getTournamentWinner() const {
    return tournamentWinner;
}

void ShootoutManager::setLoopMembersForTest(const std::vector<std::array<uint8_t, 6>>& members) {
    testLoopMembers = members;
    testLoopMembersOverride = !members.empty();
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::getLoopMembers() const {
    if (testLoopMembersOverride) return testLoopMembers;
    return buildLoopMemberSet();
}

void ShootoutManager::resetToIdle() {
    LOG_W(TAG, "resetToIdle from phase=%d", static_cast<int>(phase));
    resetTournamentState();
    // Ring-open / terminal-screen exit: an ex-coordinator that kept the anchor
    // would ignore the next ring's bracket.
    memset(coordinatorMac.data(), 0, 6);
    ringMembers.clear();
    ringClosedRebroadcastTimer.invalidate();
}

void ShootoutManager::resetTournamentState() {
    phase = Phase::IDLE;
    confirmedSet.clear();
    bracket.clear();
    currentRound.clear();
    bracketPendingAcks.clear();
    matchStartPendingAcks.clear();
    tournamentEndPendingAcks.clear();
    matchResultPendingAcks.clear();
    eliminated.clear();
    reportedLocalWin = false;
    names.clear();
    lastObservedBracketSeqId = 0;
    lastObservedMatchStartSeqId = 0;
    lastObservedTournamentEndSeqId = 0;
    currentMatchIndex = -1;
    memset(tournamentWinner.data(), 0, 6);
    memset(opponentMac.data(), 0, 6);
    memset(currentDuelistA.data(), 0, 6);
    memset(currentDuelistB.data(), 0, 6);
    if (originalIsHunter && player) {
        player->setIsHunter(*originalIsHunter);
    }
    originalIsHunter.reset();
}

void ShootoutManager::startProposal() {
    LOG_W(TAG, "startProposal");
    resetTournamentState();
    // An abort clears the anchor while the cables stay put, and the RDC latch is
    // edge-triggered, so a ring that is still closed will never announce itself
    // again. Re-make the claim here instead of waiting for an edge that is spent.
    if (ringMembers.empty() && rdc != nullptr && rdc->getChainRole() == ChainRole::RING) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr) {
            memcpy(coordinatorMac.data(), selfMac, 6);
            ringMembers = getLoopMembers();
            LOG_W(TAG, "ring still closed; re-claiming members=%zu", ringMembers.size());
            sendRingClosed();
        }
    }
    if (player) {
        originalIsHunter = player->isHunter();
    }
    phase = Phase::PROPOSAL;
}

void ShootoutManager::onRingClosed() {
    if (phase != Phase::IDLE) {
        LOG_W(TAG, "onRingClosed ignored; phase=%d", static_cast<int>(phase));
        return;
    }
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) {
        LOG_E(TAG, "onRingClosed with no local MAC");
        return;
    }
    // No election: the RDC fires this on the device whose own MAC came back around
    // the ring, and that head is the coordinator by construction. Two heads can
    // hold that claim at once while a merge settles — onRingClosedReceived breaks
    // the tie.
    memcpy(coordinatorMac.data(), selfMac, 6);
    ringMembers = getLoopMembers();
    LOG_W(TAG, "ring closed; coordinator=self members=%zu", ringMembers.size());
    sendRingClosed();
}

void ShootoutManager::onRingClosedReceived(
    const uint8_t* fromMac, const std::vector<std::array<uint8_t, 6>>& members) {
    if (phase != Phase::IDLE) return;
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) {
        LOG_E(TAG, "onRingClosedReceived with no local MAC");
        return;
    }
    // A broadcast reaches every ring in radio range; the roster is what says
    // whether this closure is ours.
    if (!containsMac(members, selfMac)) return;
    // Both heads of a merging pair latch and both announce, so an unconditional
    // adopt has A following B while B follows A and the ring runs with no
    // coordinator at all — nothing generates a bracket and every member parks in
    // BracketReveal. Same rule as the bracket stand-down: lower MAC owns the ring.
    if (isCoordinator() && memcmp(fromMac, selfMac, 6) >= 0) return;
    memcpy(coordinatorMac.data(), fromMac, 6);
    ringMembers = members;
    LOG_W(TAG, "ring closed by %s members=%zu", MacToString(fromMac), members.size());
}

bool ShootoutManager::shouldEnterProposal() const {
    if (phase != Phase::IDLE) return false;
    // ringMembers is the copy RING_CLOSED leaves on a member. A coordinator that
    // has been reset holds none, so the live RDC role is what re-opens the door
    // there — a latched copy alone would keep a still-cabled ring shut forever.
    if (!ringMembers.empty()) return true;
    return rdc != nullptr && rdc->getChainRole() == ChainRole::RING;
}

void ShootoutManager::sendRingClosed() {
    // seqId 0: receipt is idempotent and deduped by phase, so no ack round-trip.
    std::vector<uint8_t> packet = buildMacListPacket(ShootoutCmd::RING_CLOSED, 0, ringMembers);
    broadcastToRing(ringMembers, packet.data(), packet.size());
    ringClosedRebroadcastTimer.setTimer(kConfirmRebroadcastMs);
}

void ShootoutManager::confirmLocal() {
    // Gate on PROPOSAL: stale ShootoutProposal button callbacks can fire in
    // later phases and re-advance the bracket if not guarded.
    if (phase != Phase::PROPOSAL) {
        LOG_W(TAG, "confirmLocal ignored; phase=%d", static_cast<int>(phase));
        return;
    }
    LOG_W(TAG, "confirmLocal; confirmedCount before=%zu", confirmedSet.size());
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), selfMac, 6);
    if (!hasConfirmed(mac.data())) {
        confirmedSet.push_back(mac);
    }
    if (player != nullptr) {
        recordName(selfMac, player->getName().c_str());
    }
    sendLocalConfirm();
    if (allMembersConfirmed()) {
        LOG_W(TAG, "allMembersConfirmed -> advanceToBracketReveal");
        advanceToBracketReveal();
    }
}

void ShootoutManager::onConfirmReceived(const uint8_t* fromMac, const char* name) {
    if (phase != Phase::PROPOSAL) return;
    // Fast path: already-confirmed peers bypass the loop-membership scan (this
    // is the common case during 1Hz rebroadcasts — the gate only needs to
    // block first-time stray CONFIRMs from outside the ring).
    if (!hasConfirmed(fromMac) && !containsMac(getLoopMembers(), fromMac)) return;
    recordName(fromMac, name);
    bool added = !hasConfirmed(fromMac);
    if (added) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), fromMac, 6);
        confirmedSet.push_back(mac);
        LOG_W(TAG, "onConfirmReceived from=%s count=%zu",
              MacToString(fromMac), confirmedSet.size());
    }
    if (allMembersConfirmed()) {
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
    for (auto& entry : names) {
        if (memcmp(entry.mac.data(), mac, 6) == 0) {
            entry.name = buf;
            return;
        }
    }
    NameEntry e;
    memcpy(e.mac.data(), mac, 6);
    e.name = buf;
    names.push_back(std::move(e));
}

std::string ShootoutManager::getNameForMac(const uint8_t* mac) const {
    for (const auto& entry : names) {
        if (memcmp(entry.mac.data(), mac, 6) == 0) return entry.name;
    }
    char fallback[4];
    snprintf(fallback, sizeof(fallback), "%02X", mac[5]);
    return fallback;
}

bool ShootoutManager::hasConfirmed(const uint8_t* mac) const {
    return containsMac(confirmedSet, mac);
}

bool ShootoutManager::allMembersConfirmed() const {
    auto members = getLoopMembers();
    // Roster announces can still be in flight when the ring closes; a one-entry
    // roster would run a solo tournament this device wins the instant it starts.
    if (members.size() < 2) return false;
    for (const auto& m : members) {
        if (!hasConfirmed(m.data())) return false;
    }
    return true;
}

std::array<uint8_t, 6> ShootoutManager::getCoordinatorMac() const {
    return coordinatorMac;
}

bool ShootoutManager::isCoordinator() const {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return false;
    auto coord = getCoordinatorMac();
    return memcmp(coord.data(), selfMac, 6) == 0;
}

void ShootoutManager::generateBracket() {
    bracket = confirmedSet;
    // std::random_device is deterministic under newlib on ESP32, so seed
    // from platform clock XOR self-MAC to get real variation.
    unsigned long seed = 0;
    auto* clk = SimpleTimer::getPlatformClock();
    if (clk != nullptr) seed = clk->milliseconds();
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac != nullptr) {
        for (int i = 0; i < 6; i++) {
            seed ^= static_cast<unsigned long>(selfMac[i]) << ((i % 4) * 8);
        }
    }
    std::mt19937 rng(seed);
    std::shuffle(bracket.begin(), bracket.end(), rng);
    currentRound = bracket;
}

void ShootoutManager::primeMatchManagerForMatch() {
    if (!matchManager) return;
    if (!isLocalDuelist()) return;

    // Role-for-this-match from MAC ordering: both sides compute the same
    // ordering so the hunter_draw_time and bounty_time slots in MatchManager
    // are written by exactly one duelist each.
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    bool localIsHunterForMatch = selfMac != nullptr &&
                                 memcmp(selfMac, opponentMac.data(), 6) < 0;
    if (player) player->setIsHunter(localIsHunterForMatch);

    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    deriveShootoutMatchId(currentMatchIndex, matchId, sizeof(matchId));
    LOG_W(TAG, "primeMatchManagerForMatch matchIndex=%d localHunter=%d",
          currentMatchIndex, localIsHunterForMatch);
    matchManager->initializeShootoutMatch(matchId, opponentMac.data());
}

void ShootoutManager::advanceToBracketReveal() {
    phase = Phase::BRACKET_REVEAL;
    bracketRevealTimer.setTimer(kBracketRevealMs);
    if (isCoordinator()) {
        generateBracket();
        sendBracketToPeers();
    }
}

unsigned long ShootoutManager::ackTimeoutForRetry(uint8_t retries) {
    constexpr size_t kTableSize = sizeof(kAckBackoffMs) / sizeof(kAckBackoffMs[0]);
    if (retries >= kTableSize) return kAckBackoffMs[kTableSize - 1];
    return kAckBackoffMs[retries];
}

std::vector<uint8_t> ShootoutManager::buildMacListPacket(
    ShootoutCmd cmd, uint8_t seqId,
    const std::vector<std::array<uint8_t, 6>>& macs) const {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(cmd));
    packet.push_back(seqId);
    // The roster is the RDC's 64 plus self, so it can land one over what the
    // decoder accepts — and an over-long frame is dropped by every receiver, not
    // just the members past the cap. Truncating keeps the ring running.
    size_t count = macs.size();
    if (count > MAX_BRACKET_SIZE) {
        LOG_E(TAG, "mac list %zu over cap %u; truncating", count,
              static_cast<unsigned>(MAX_BRACKET_SIZE));
        count = MAX_BRACKET_SIZE;
    }
    packet.push_back(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; i++) {
        packet.insert(packet.end(), macs[i].begin(), macs[i].end());
    }
    return packet;
}

void ShootoutManager::sendBracketToPeers() {
    if (bracket.empty()) return;
    lastBracketSeqId = nextSeqId();
    std::vector<uint8_t> packet =
        buildMacListPacket(ShootoutCmd::BRACKET, lastBracketSeqId, bracket);
    sendReliablyToPeers(bracketPendingAcks, bracket, packet.data(), packet.size());
}

void ShootoutManager::onBracketAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastBracketSeqId) return;
    eraseFromPending(bracketPendingAcks, fromMac);
}

void ShootoutManager::abortTournament() {
    if (phase == Phase::ABORTED) return;
    LOG_W(TAG, "abortTournament from phase=%d", static_cast<int>(phase));

    // Broadcast before resetToIdle clears bracket/confirmedSet.
    uint8_t packet[2];
    packet[0] = static_cast<uint8_t>(ShootoutCmd::ABORT);
    packet[1] = 0;
    const std::vector<std::array<uint8_t, 6>>& targets = bracket.empty() ? confirmedSet : bracket;
    broadcastToRing(targets, packet, sizeof(packet));

    resetToIdle();
    phase = Phase::ABORTED;
}

void ShootoutManager::sendLocalConfirm() {
    // [cmd, seq, 6-byte MAC, kNameLength-byte null-padded name]
    uint8_t payload[2 + 6 + kNameLength];
    payload[0] = static_cast<uint8_t>(ShootoutCmd::CONFIRM);
    payload[1] = 0;
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    memcpy(&payload[2], selfMac, 6);
    memset(&payload[8], 0, kNameLength);
    if (player != nullptr) {
        const std::string& n = player->getName();
        size_t copyLen = n.size() < kNameLength ? n.size() : kNameLength;
        memcpy(&payload[8], n.data(), copyLen);
    }

    broadcastToRing(getLoopMembers(), payload, sizeof(payload));
    confirmRebroadcastTimer.setTimer(kConfirmRebroadcastMs);
}

void ShootoutManager::sync() {
    // A member that missed the closure frame stays in Idle with nothing to poll,
    // while the coordinator waits on a confirm it will never get. No ack needed:
    // a repeat is a no-op once the member is out of Phase::IDLE.
    if (phase == Phase::PROPOSAL && ringClosedRebroadcastTimer.expired() &&
        isCoordinator() && !allMembersConfirmed()) {
        // Re-read the roster: a member whose announce to the head was still in
        // flight at closure only appears in it now.
        ringMembers = getLoopMembers();
        sendRingClosed();
    }

    // Check cheap conditions before allMembersConfirmed(), which rebuilds the
    // loop-member set each call.
    if (phase == Phase::PROPOSAL && confirmRebroadcastTimer.expired()) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr && hasConfirmed(selfMac) && !allMembersConfirmed()) {
            sendLocalConfirm();
        }
    }

    if (!bracketPendingAcks.empty()) {
        std::vector<uint8_t> packet =
            buildMacListPacket(ShootoutCmd::BRACKET, lastBracketSeqId, bracket);
        // A member that never acks the bracket would sit out the tournament it
        // is physically wired into, so an exhausted budget aborts rather than
        // dropping that member.
        if (retryPendingRound(bracketPendingAcks, packet.data(), packet.size())) {
            abortTournament();
        }
    }

    maybeStartNextMatch();

    if (isCoordinator() && phase == Phase::MATCH_IN_PROGRESS &&
        matchStartPendingAcks.empty() &&
        matchStartWatchdog.expired()) {
        sendMatchStartToPeers(currentMatchIndex);
    }

    if (phase == Phase::ENDED && !tournamentEndPendingAcks.empty()) {
        uint8_t packet[8];
        packet[0] = static_cast<uint8_t>(ShootoutCmd::TOURNAMENT_END);
        packet[1] = lastTournamentEndSeqId;
        memcpy(&packet[2], tournamentWinner.data(), 6);
        retryPendingRound(tournamentEndPendingAcks, packet, sizeof(packet));
    }

    if (!matchResultPendingAcks.empty()) {
        std::vector<uint8_t> packet = buildMatchResultPacket(
            lastMatchResult.winner.data(), lastMatchResult.loser.data(),
            lastMatchResult.matchIndex);
        retryPendingRound(matchResultPendingAcks, packet.data(), packet.size());
    }
}

std::pair<std::array<uint8_t,6>, std::array<uint8_t,6>>
ShootoutManager::getCurrentMatchPair() const {
    if (currentMatchIndex < 0) return {};
    return {currentDuelistA, currentDuelistB};
}

std::vector<uint8_t> ShootoutManager::buildMatchStartPacket(int matchIndex) const {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(ShootoutCmd::MATCH_START));
    packet.push_back(lastMatchStartSeqId);
    const std::array<uint8_t, 6>& a = currentRound[matchIndex * 2];
    const std::array<uint8_t, 6>& b = currentRound[matchIndex * 2 + 1];
    packet.insert(packet.end(), a.begin(), a.end());
    packet.insert(packet.end(), b.begin(), b.end());
    packet.push_back(static_cast<uint8_t>(matchIndex));
    return packet;
}

void ShootoutManager::sendMatchStartToPeers(int matchIndex) {
    lastMatchStartSeqId = nextSeqId();

    auto packet = buildMatchStartPacket(matchIndex);
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    const std::array<uint8_t, 6>& a = currentRound[matchIndex * 2];
    const std::array<uint8_t, 6>& b = currentRound[matchIndex * 2 + 1];
    bool sameMatch = isSameMatch(matchIndex, a.data(), b.data());
    currentDuelistA = a;
    currentDuelistB = b;
    currentMatchIndex = matchIndex;
    if (!sameMatch) reportedLocalWin = false;
    if (!sameMatch && isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, a.data(), 6) == 0) ? b.data() : a.data();
        memcpy(opponentMac.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    sendReliablyToPeers(matchStartPendingAcks, bracket, packet.data(), packet.size());
    matchStartWatchdog.setTimer(kMatchWatchdogMs);
}

void ShootoutManager::onMatchStartAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastMatchStartSeqId) return;
    eraseFromPending(matchStartPendingAcks, fromMac);
}

bool ShootoutManager::isSameMatch(int matchIndex, const uint8_t* a, const uint8_t* b) const {
    return matchIndex == currentMatchIndex && phase == Phase::MATCH_IN_PROGRESS && memcmp(currentDuelistA.data(), a, 6) == 0 && memcmp(currentDuelistB.data(), b, 6) == 0;
}

bool ShootoutManager::isActiveDuelist(const uint8_t* mac) const {
    if (currentMatchIndex < 0) return false;
    auto pair = getCurrentMatchPair();
    return memcmp(pair.first.data(), mac, 6) == 0 ||
           memcmp(pair.second.data(), mac, 6) == 0;
}

void ShootoutManager::onLocalRDCDisconnect(const uint8_t* lostMac) {
    // Gate before the log: this now fires on every direct-peer link death, and
    // outside a tournament that is ordinary chain-duel unplugging. LOG_W survives
    // the release build, so logging first put a line on the wire per cable pull.
    if (phase == Phase::IDLE || phase == Phase::ABORTED || phase == Phase::ENDED) return;
    LOG_W(TAG, "onLocalRDCDisconnect %s phase=%d",
          MacToString(lostMac), static_cast<int>(phase));
    if (rdc && rdc->canReachPeer(lostMac)) return;
    uint8_t packet[8];
    packet[0] = static_cast<uint8_t>(ShootoutCmd::PEER_LOST);
    packet[1] = 0;
    memcpy(&packet[2], lostMac, 6);
    const std::vector<std::array<uint8_t, 6>>& targets = bracket.empty() ? confirmedSet : bracket;
    broadcastToRing(targets, packet, sizeof(packet));
    // Locally observed: this device's own jack went quiet, so no ring filter.
    applyPeerLoss(lostMac);
}

void ShootoutManager::onPeerLostReceived(const uint8_t* lostMac) {
    // A broadcast PEER_LOST reaches every ring in range; only a loss inside ours
    // may end our tournament.
    if (!isRingMember(lostMac)) return;
    applyPeerLoss(lostMac);
}

void ShootoutManager::applyPeerLoss(const uint8_t* lostMac) {
    LOG_W(TAG, "applyPeerLoss %s phase=%d",
          MacToString(lostMac), static_cast<int>(phase));
    if (phase == Phase::IDLE || phase == Phase::ABORTED || phase == Phase::ENDED) return;
    if (rdc && rdc->canReachPeer(lostMac)) return;
    abortTournament();
}

void ShootoutManager::maybeStartNextMatch() {
    if (!isCoordinator()) return;
    if (!bracketPendingAcks.empty()) return;
    if (phase != Phase::BRACKET_REVEAL && phase != Phase::BETWEEN_MATCHES) return;
    if (phase == Phase::BRACKET_REVEAL && !bracketRevealTimer.expired()) return;
    // Re-entrancy guard: this function mutates currentMatchIndex, bracket,
    // and phase; concurrent entry from sync() and ESP-NOW recv callbacks
    // (Core 0 vs main loop) would double-advance the bracket.
    if (inMaybeStartNextMatch) return;
    inMaybeStartNextMatch = true;
    struct Guard {
        bool& f;
        ~Guard() { f = false; }
    } guard{inMaybeStartNextMatch};

    currentMatchIndex++;
    int pairEnd = currentMatchIndex * 2 + 1;
    if (pairEnd >= static_cast<int>(currentRound.size())) {
        std::vector<std::array<uint8_t, 6>> survivors;
        for (const auto& m : currentRound) {
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
              currentRound.size(), survivors.size());
        currentRound = survivors;
        currentMatchIndex = 0;
    }
    sendMatchStartToPeers(currentMatchIndex);
    phase = Phase::MATCH_IN_PROGRESS;
}

void ShootoutManager::onBracketReceived(
    const uint8_t* fromMac, const std::vector<std::array<uint8_t, 6>>& offeredBracket,
    uint8_t seqId) {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    // BRACKET is a broadcast, so a tournament two rings away lands here too, and
    // the roster is the only thing that says whether this one is ours. Ahead of
    // the stand-down, not after it: standing down on a bracket we are not in
    // drops our own and adopts nothing, and no phase past IDLE re-runs the claim.
    if (!containsMac(offeredBracket, selfMac)) return;
    if (isCoordinator()) {
        // Merge-collision tiebreaker: two rings that each closed and claimed a
        // head can be cabled together after both claimed. Lower MAC owns the
        // merged tournament, so a bracket from below us demotes this device into
        // a follower — dropping the self-built bracket, not just the anchor, or
        // both sides keep running their own (split brain).
        if (selfMac == nullptr || memcmp(fromMac, selfMac, 6) >= 0) return;
        LOG_W(TAG, "coordinator stand-down to %s", MacToString(fromMac));
        memset(coordinatorMac.data(), 0, 6);
        bracket.clear();
        currentRound.clear();
        eliminated.clear();
        bracketPendingAcks.clear();
        matchStartPendingAcks.clear();
        currentMatchIndex = -1;
        reportedLocalWin = false;
    }
    if (seqId != 0 && seqId == lastObservedBracketSeqId) {
        sendShootoutAck(ShootoutCmd::BRACKET, seqId, fromMac);
        return;
    }
    lastObservedBracketSeqId = seqId;
    bracket = offeredBracket;
    currentRound = offeredBracket;
    memcpy(coordinatorMac.data(), fromMac, 6);
    phase = Phase::BRACKET_REVEAL;
    bracketRevealTimer.setTimer(kBracketRevealMs);
    sendShootoutAck(ShootoutCmd::BRACKET, seqId, coordinatorMac.data());
}

void ShootoutManager::onMatchStartReceived(
    const uint8_t* duelistA, const uint8_t* duelistB,
    uint8_t matchIndex, uint8_t seqId) {
    if (isCoordinator()) return;
    // Both duelists come from our own bracket, so a pair naming anyone outside
    // it belongs to another ring's broadcast.
    if (!containsMac(bracket, duelistA) || !containsMac(bracket, duelistB)) return;
    if (seqId != 0 && seqId == lastObservedMatchStartSeqId) {
        sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coordinatorMac.data());
        return;
    }
    bool sameMatch = isSameMatch(matchIndex, duelistA, duelistB);
    lastObservedMatchStartSeqId = seqId;
    if (sameMatch) {
        sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coordinatorMac.data());
        return;
    }
    currentMatchIndex = matchIndex;
    memcpy(currentDuelistA.data(), duelistA, 6);
    memcpy(currentDuelistB.data(), duelistB, 6);
    phase = Phase::MATCH_IN_PROGRESS;
    reportedLocalWin = false;
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, duelistA, 6) == 0) ? duelistB : duelistA;
        memcpy(opponentMac.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coordinatorMac.data());
}

void ShootoutManager::sendShootoutAck(ShootoutCmd cmd, uint8_t seqId, const uint8_t* toMac) {
    ShootoutAckPayload ack{cmd, seqId};
    wirelessManager->sendEspNowData(toMac, PktType::kShootoutCommandAck,
                                    reinterpret_cast<uint8_t*>(&ack), sizeof(ack));
}

bool ShootoutManager::isEliminated(const uint8_t* mac) const {
    return containsMac(eliminated, mac);
}

void ShootoutManager::applyMatchResult(const uint8_t* winner, const uint8_t* loser) {
    if (!isEliminated(loser)) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), loser, 6);
        eliminated.push_back(mac);
    }
    // Restore pre-tournament role at each match boundary. primeMatchManagerForMatch
    // re-applies the per-match override on the next match start if this device is a
    // duelist again. Prevents role from staying flipped when the tournament ends.
    if (originalIsHunter && player) {
        player->setIsHunter(*originalIsHunter);
    }
    phase = Phase::BETWEEN_MATCHES;
    matchStartWatchdog.invalidate();
}

std::vector<uint8_t> ShootoutManager::buildMatchResultPacket(
    const uint8_t* winner, const uint8_t* loser, uint8_t matchIndex) const {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(ShootoutCmd::MATCH_RESULT));
    packet.push_back(lastMatchResultSeqId);
    packet.insert(packet.end(), winner, winner + 6);
    packet.insert(packet.end(), loser, loser + 6);
    packet.push_back(matchIndex);
    return packet;
}

void ShootoutManager::sendMatchResultToPeers(
    const uint8_t* winner, const uint8_t* loser, uint8_t matchIndex) {
    lastMatchResultSeqId = nextSeqId();
    memcpy(lastMatchResult.winner.data(), winner, 6);
    memcpy(lastMatchResult.loser.data(), loser, 6);
    lastMatchResult.matchIndex = matchIndex;
    auto packet = buildMatchResultPacket(winner, loser, matchIndex);
    // Targets confirmedSet to reach already-eliminated players too.
    sendReliablyToPeers(matchResultPendingAcks, confirmedSet, packet.data(), packet.size());
}

void ShootoutManager::reportLocalWin() {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac == nullptr) return;
    if (reportedLocalWin) return;
    reportedLocalWin = true;
    LOG_W(TAG, "reportLocalWin matchIndex=%d", currentMatchIndex);
    sendMatchResultToPeers(selfMac, opponentMac.data(), static_cast<uint8_t>(currentMatchIndex));
    applyMatchResult(selfMac, opponentMac.data());
    if (isCoordinator()) maybeStartNextMatch();
}

void ShootoutManager::onMatchResultReceived(
    const uint8_t* winner, const uint8_t* loser,
    uint8_t matchIndex, uint8_t seqId, const uint8_t* fromMac) {
    // Winner and loser are both drawn from our own bracket, so a result naming
    // anyone outside it came from another ring's broadcast — and must not be
    // acked, or that ring's sender stops retrying to its real audience.
    if (!containsMac(bracket, winner) || !containsMac(bracket, loser)) return;
    // Always ack so the sender stops retrying, even when this is a duplicate.
    sendShootoutAck(ShootoutCmd::MATCH_RESULT, seqId, fromMac);
    // Dedup by loser-MAC rather than seqId: non-coord senders have independent
    // seq counters, but each loser is eliminated exactly once per tournament.
    if (isEliminated(loser)) {
        return;
    }
    LOG_W(TAG, "onMatchResultReceived matchIndex=%u", matchIndex);
    applyMatchResult(winner, loser);
    if (isCoordinator()) maybeStartNextMatch();
}

void ShootoutManager::onMatchResultAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastMatchResultSeqId) return;
    eraseFromPending(matchResultPendingAcks, fromMac);
}

size_t ShootoutManager::getMatchResultPendingAckCount() const {
    return matchResultPendingAcks.size();
}

std::array<uint8_t, 6> ShootoutManager::findLastRemaining() const {
    for (const auto& m : bracket) {
        if (!isEliminated(m.data())) return m;
    }
    return {};
}

void ShootoutManager::sendTournamentEndToPeers(const uint8_t* winner) {
    LOG_W(TAG, "tournamentEnd winner=%s", MacToString(winner));
    lastTournamentEndSeqId = nextSeqId();
    uint8_t packet[8];
    packet[0] = static_cast<uint8_t>(ShootoutCmd::TOURNAMENT_END);
    packet[1] = lastTournamentEndSeqId;
    memcpy(&packet[2], winner, 6);
    // Targets confirmedSet rather than bracket: eliminated players need the
    // tournament-end transition or they stall in BETWEEN_MATCHES.
    sendReliablyToPeers(tournamentEndPendingAcks, confirmedSet, packet, sizeof(packet));
    memcpy(tournamentWinner.data(), winner, 6);
    phase = Phase::ENDED;
}

void ShootoutManager::onTournamentEndAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastTournamentEndSeqId) return;
    eraseFromPending(tournamentEndPendingAcks, fromMac);
}

void ShootoutManager::onTournamentEndReceived(const uint8_t* winner, uint8_t seqId) {
    // The winner is a member of our bracket; anyone else won another ring's
    // tournament and must not end ours.
    if (!containsMac(bracket, winner)) return;
    if (seqId != 0 && seqId == lastObservedTournamentEndSeqId) {
        auto coord = getCoordinatorMac();
        sendShootoutAck(ShootoutCmd::TOURNAMENT_END, seqId, coord.data());
        return;
    }
    lastObservedTournamentEndSeqId = seqId;
    memcpy(tournamentWinner.data(), winner, 6);
    phase = Phase::ENDED;
    auto coord = getCoordinatorMac();
    sendShootoutAck(ShootoutCmd::TOURNAMENT_END, seqId, coord.data());
}

void ShootoutManager::onAbortReceived(const uint8_t* fromMac) {
    // Without this filter a neighbouring ring's abort would tear down every
    // tournament in radio range.
    if (!isRingMember(fromMac)) return;
    if (phase == Phase::ABORTED || phase == Phase::IDLE) return;
    resetToIdle();
    phase = Phase::ABORTED;
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::buildLoopMemberSet() const {
    // The RDC serves a roster only where it is the authority, which inside a ring
    // is the head that latched it. Every other member holds the copy that head
    // sent with RING_CLOSED.
    if (rdc == nullptr || rdc->getChainRole() != ChainRole::RING) return ringMembers;

    std::vector<std::array<uint8_t, 6>> members = rdc->getChainMembers();
    // getChainMembers() enumerates the devices that announced to the head, never
    // the head itself, and the coordinator is a participant like any other.
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    if (selfMac != nullptr && !containsMac(members, selfMac)) {
        std::array<uint8_t, 6> self;
        memcpy(self.data(), selfMac, 6);
        members.push_back(self);
    }
    return members;
}
