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
    , rdc(rdc)
    , resender(wirelessManager, Resender::BudgetPolicy::EVERY_ROUND) {
    resender.setAbandonCallback(
        [this](PktType, uint8_t seqId, const uint8_t* targetMac,
               const uint8_t* packet, size_t len) {
            onCommandAbandoned(seqId, targetMac, packet, len);
        });
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

size_t ShootoutManager::getPendingAckCount(uint8_t seqId) const {
    return resender.pendingCount(PktType::kShootoutCommand, seqId);
}

uint8_t ShootoutManager::getLastBracketSeqId() const {
    return lastBracketSeqId;
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

// The single seqId allocator for every reliably-sent command this manager
// sends — the four that ride the Resender; the rest go out unsequenced. That
// is load-bearing, not incidental: because one counter serves all of them, a
// seqId in flight names exactly one of this device's frames, which is what lets
// an ack be answered on its seqId alone.
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
    if (peersExcludingSelf(peers).empty()) return;
    broadcastCommand(packet, len);
}

// Who a ring fan-out is addressed to. One spelling, asked by both send paths, so
// a device can never end up owing an ack to itself.
std::vector<std::array<uint8_t, 6>> ShootoutManager::peersExcludingSelf(
    const std::vector<std::array<uint8_t, 6>>& peers) const {
    const uint8_t* selfMac = wirelessManager->getMacAddress();
    std::vector<std::array<uint8_t, 6>> others;
    for (const std::array<uint8_t, 6>& m : peers) {
        if (selfMac != nullptr && memcmp(m.data(), selfMac, 6) == 0) continue;
        others.push_back(m);
    }
    return others;
}

void ShootoutManager::sendReliablyToPeers(const std::vector<std::array<uint8_t, 6>>& peers,
                                          uint8_t seqId, const uint8_t* packet, size_t len) {
    resender.sendBroadcast(peersExcludingSelf(peers), PktType::kShootoutCommand,
                           seqId, packet, len);
}

void ShootoutManager::onCommandAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    // seqId alone names the fan-out: nextSeqId() is the single allocator for all
    // four command families, so no two frames in flight from this device share
    // one. Cross-checking the ack's command against a per-family cursor would
    // catch nothing — an ack echoes both fields out of the frame it answers —
    // and would refuse a valid ack for a still-armed frame that is no longer its
    // family's latest.
    resender.onAck(PktType::kShootoutCommand, seqId, fromMac);
}

void ShootoutManager::onCommandAbandoned(uint8_t seqId, const uint8_t* targetMac,
                                         const uint8_t* packet, size_t len) {
    if (len == 0) return;
    const ShootoutCmd cmd = static_cast<ShootoutCmd>(packet[0]);

    // Only silence that actually blocks the tournament ends it. BRACKET is the
    // roster, so a member still in the running who never received it cannot take
    // part at all. MATCH_START only matters to the two devices fighting: a
    // spectator missing it just does not see that round, and ending a tournament
    // for that would put an abort opportunity on every member of every match
    // rather than one on the bracket.
    //
    // Both questions are asked of the frame that was abandoned, not of the
    // manager's current state. A fan-out outlives the match it announced — it
    // keeps retrying a silent recipient for over a second — so by the time it is
    // given up on, currentDuelist* may already name a different match, and a
    // finished tournament still names its final pair.
    bool blocksTournament = false;
    if (cmd == ShootoutCmd::BRACKET) {
        blocksTournament = containsMac(bracket, targetMac) && !isEliminated(targetMac);
    } else if (cmd == ShootoutCmd::MATCH_START && len >= 14) {
        // [cmd, seqId, duelistA(6), duelistB(6), matchIndex] — see
        // buildMatchStartPacket.
        blocksTournament = memcmp(&packet[2], targetMac, 6) == 0 ||
                           memcmp(&packet[8], targetMac, 6) == 0;
    } else if (cmd == ShootoutCmd::MATCH_RESULT && len >= 15 &&
               memcmp(targetMac, coordinatorMac.data(), 6) == 0) {
        // Nothing on the coordinator's side notices a result that never lands: it
        // advances only on receiving one, so it waits with nothing owed. This
        // device is the one that knows, so it says so again. One attempt per
        // bout — a second abandonment cannot distinguish a lost result from a
        // lost ack, and guessing is worse than staying quiet.
        if (matchResultResentIndex != static_cast<int>(packet[14])) {
            matchResultResentIndex = static_cast<int>(packet[14]);
            LOG_W(TAG, "coordinator missed our match result; re-sending");
            // Off the abandoned frame, not current state: a fan-out outlives the
            // match it announced. [cmd, seqId, winner(6), loser(6), matchIndex]
            sendMatchResultToPeers(&packet[2], &packet[8], packet[14]);
        }
    }

    if (blocksTournament) {
        LOG_E(TAG, "shootout cmd=%u seq=%u unacked by %s; ending tournament",
              (unsigned)packet[0], (unsigned)seqId, MacToString(targetMac));
        abortTournament();
        return;
    }
    LOG_W(TAG, "shootout cmd=%u seq=%u unacked by %s",
          (unsigned)packet[0], (unsigned)seqId, MacToString(targetMac));
}

std::array<uint8_t, 6> ShootoutManager::getOpponentMac() const {
    return opponentMac;
}

uint8_t ShootoutManager::getLastMatchStartSeqId() const {
    return lastMatchStartSeqId;
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
    // Every fan-out this tournament had in flight is void; a retransmit landing
    // after the reset would speak for a tournament that no longer exists.
    resender.cancelAll(PktType::kShootoutCommand);
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
    sendReliablyToPeers(bracket, lastBracketSeqId, packet.data(), packet.size());
}

void ShootoutManager::abortTournament() {
    if (phase == Phase::ABORTED) return;
    // A tournament that reached its winner is over, not stuck. A late
    // abandonment from a fan-out that outlived the final match must not tear
    // down the standings — and the ABORT would be applied ring-wide, wiping the
    // winner screen on every device.
    if (phase == Phase::ENDED) return;
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

    // Every command family retransmits and abandons here; which one gave up is
    // read off the frame in onCommandAbandoned.
    resender.sync();

    maybeStartNextMatch();
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
    if (!sameMatch) {
        reportedLocalWin = false;
    }
    if (!sameMatch && isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, a.data(), 6) == 0) ? b.data() : a.data();
        memcpy(opponentMac.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    sendReliablyToPeers(bracket, lastMatchStartSeqId, packet.data(), packet.size());
}

bool ShootoutManager::isSameMatch(int matchIndex, const uint8_t* a, const uint8_t* b) const {
    return matchIndex == currentMatchIndex && phase == Phase::MATCH_IN_PROGRESS && memcmp(currentDuelistA.data(), a, 6) == 0 && memcmp(currentDuelistB.data(), b, 6) == 0;
}

void ShootoutManager::onLocalRDCDisconnect(const uint8_t* lostMac) {
    // Gate before the log: this fires on every direct-peer link death, and
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
    // Nobody moves on until the whole bracket has it.
    if (getPendingAckCount(lastBracketSeqId) > 0) return;
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
        resender.cancelAll(PktType::kShootoutCommand);
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
    // A bout whose loser is already out has been played. maybeStartNextMatch
    // waits only on the BRACKET fan-out, never on MATCH_START's, so the
    // coordinator can announce match N+1 while match N's fan-out is still
    // retrying a member that went quiet. That member's cursor has moved to N+1,
    // so the dedup above misses the late N frame and isSameMatch is false —
    // without this it would be dragged back into a bout it already finished and
    // re-primed against an opponent it already beat.
    if (isEliminated(duelistA) || isEliminated(duelistB)) {
        lastObservedMatchStartSeqId = seqId;
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

void ShootoutManager::applyMatchResult(const uint8_t* winner, const uint8_t* loser,
                                       bool endsCurrentBout) {
    if (!isEliminated(loser)) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), loser, 6);
        eliminated.push_back(mac);
    }
    if (!endsCurrentBout) return;
    // Restore pre-tournament role at each match boundary. primeMatchManagerForMatch
    // re-applies the per-match override on the next match start if this device is a
    // duelist again. Prevents role from staying flipped when the tournament ends.
    if (originalIsHunter && player) {
        player->setIsHunter(*originalIsHunter);
    }
    phase = Phase::BETWEEN_MATCHES;
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
    auto packet = buildMatchResultPacket(winner, loser, matchIndex);
    // Targets bracket, not confirmedSet. Both reach eliminated players — only
    // currentRound shrinks — but confirmedSet is each device's own tally of the
    // CONFIRMs it happened to hear, and those are unacked broadcasts sent once
    // per press. A follower that missed the coordinator's would never track it
    // as a recipient, so its result could never abandon against the coordinator
    // and the recovery below could never fire. The bracket is the coordinator's
    // own roster, acked on arrival.
    sendReliablyToPeers(bracket, lastMatchResultSeqId, packet.data(), packet.size());
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
    // Record the elimination, but only let a result end the bout it belongs to.
    // A result can arrive late — its sender re-sends when the coordinator misses
    // one — and a device that has since been paired into a newer match would
    // otherwise be pulled out of it mid-duel and have its per-match role
    // restored underneath it, leaving both duelists computing the same role and
    // neither reporting a win.
    const bool namesCurrentBout =
        currentMatchIndex < 0 || static_cast<int>(matchIndex) == currentMatchIndex;
    applyMatchResult(winner, loser, namesCurrentBout);
    if (isCoordinator()) maybeStartNextMatch();
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
    sendReliablyToPeers(confirmedSet, lastTournamentEndSeqId, packet, sizeof(packet));
    memcpy(tournamentWinner.data(), winner, 6);
    phase = Phase::ENDED;
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
    // Same guard as abortTournament, and reachable: a member that missed
    // TOURNAMENT_END is still in BETWEEN_MATCHES, so a cable pulled after the
    // winner appears sends ABORT to devices already showing the result.
    if (phase == Phase::ENDED) return;
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
