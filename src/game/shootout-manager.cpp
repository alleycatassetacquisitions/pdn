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
constexpr unsigned long kAckBackoffMs[] = {100, 200, 400, 800, 1600};

void deriveShootoutMatchId(int matchIndex, char* out, size_t outSize) {
    // Deterministic ID so both duelists prime MatchManager with the same
    // value without a SEND_MATCH_ID handshake.
    snprintf(out, outSize, "SHT-%032d", matchIndex);
}
}

ShootoutManager::ShootoutManager(Player* player,
                                 WirelessManager* wirelessManager,
                                 RemoteDeviceCoordinator* rdc,
                                 ChainDuelManager* cdm)
    : player_(player), wirelessManager_(wirelessManager), rdc_(rdc), cdm_(cdm) {}

bool ShootoutManager::active() const {
    return phase_ != Phase::IDLE;
}

ShootoutManager::Phase ShootoutManager::getPhase() const {
    return phase_;
}

size_t ShootoutManager::getConfirmedCount() const {
    return confirmedSet_.size();
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::getBracket() const {
    return bracket_;
}

bool ShootoutManager::hasBye() const {
    return bracket_.size() % 2 == 1;
}

uint8_t ShootoutManager::getLastBracketSeqId() const {
    return lastBracketSeqId_;
}

size_t ShootoutManager::getBracketPendingAckCount() const {
    return bracketPendingAcks_.size();
}

int ShootoutManager::getCurrentMatchIndex() const {
    return currentMatchIndex_;
}

bool ShootoutManager::isLocalDuelist() const {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return false;
    return memcmp(selfMac, currentDuelistA_.data(), 6) == 0 ||
           memcmp(selfMac, currentDuelistB_.data(), 6) == 0;
}

uint8_t ShootoutManager::nextSeqId() {
    uint8_t id = nextShootoutSeqId_++;
    if (nextShootoutSeqId_ == 0) nextShootoutSeqId_ = 1;
    return id;
}

void ShootoutManager::sendToPeers(const std::vector<std::array<uint8_t, 6>>& peers,
                                  const uint8_t* packet, size_t len) {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    for (const auto& m : peers) {
        if (selfMac != nullptr && memcmp(m.data(), selfMac, 6) == 0) continue;
        wirelessManager_->sendEspNowData(m.data(), PktType::kShootoutCommand, packet, len);
    }
}

void ShootoutManager::sendReliablyToPeers(std::vector<BracketPending>& pending,
                                          const std::vector<std::array<uint8_t, 6>>& peers,
                                          const uint8_t* packet, size_t len) {
    sendToPeers(peers, packet, len);
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    pending.clear();
    for (const auto& m : peers) {
        if (selfMac != nullptr && memcmp(m.data(), selfMac, 6) == 0) continue;
        BracketPending p;
        p.peer = m;
        p.timer.setTimer(ackTimeoutForRetry(0));
        pending.push_back(p);
    }
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
    return opponentMac_;
}

uint8_t ShootoutManager::getLastMatchStartSeqId() const {
    return lastMatchStartSeqId_;
}

size_t ShootoutManager::getTournamentEndPendingAckCount() const {
    return tournamentEndPendingAcks_.size();
}

std::array<uint8_t, 6> ShootoutManager::getTournamentWinner() const {
    return tournamentWinner_;
}

void ShootoutManager::setLoopMembersForTest(const std::vector<std::array<uint8_t, 6>>& members) {
    testLoopMembers_ = members;
    testLoopMembersOverride_ = !members.empty();
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::getLoopMembers() const {
    if (testLoopMembersOverride_) return testLoopMembers_;
    return buildLoopMemberSet();
}

void ShootoutManager::resetToIdle() {
    LOG_W(TAG, "resetToIdle from phase=%d", static_cast<int>(phase_));
    phase_ = Phase::IDLE;
    confirmedSet_.clear();
    bracket_.clear();
    currentRound_.clear();
    bracketPendingAcks_.clear();
    matchStartPendingAcks_.clear();
    tournamentEndPendingAcks_.clear();
    matchResultPendingAcks_.clear();
    eliminated_.clear();
    forfeited_.clear();
    names_.clear();
    lastObservedBracketSeqId_ = 0;
    lastObservedMatchStartSeqId_ = 0;
    lastObservedTournamentEndSeqId_ = 0;
    currentMatchIndex_ = -1;
    memset(tournamentWinner_.data(), 0, 6);
    memset(opponentMac_.data(), 0, 6);
    memset(currentDuelistA_.data(), 0, 6);
    memset(currentDuelistB_.data(), 0, 6);
    if (originalIsHunter_ && player_) {
        player_->setIsHunter(*originalIsHunter_);
    }
    originalIsHunter_.reset();
}

void ShootoutManager::startProposal() {
    LOG_W(TAG, "startProposal");
    resetToIdle();
    if (player_) {
        originalIsHunter_ = player_->isHunter();
    }
    phase_ = Phase::PROPOSAL;
}

void ShootoutManager::confirmLocal() {
    // Gate on PROPOSAL: stale ShootoutProposal button callbacks can fire in
    // later phases and re-advance the bracket if not guarded.
    if (phase_ != Phase::PROPOSAL) {
        LOG_W(TAG, "confirmLocal ignored; phase=%d", static_cast<int>(phase_));
        return;
    }
    LOG_W(TAG, "confirmLocal; confirmedCount before=%zu", confirmedSet_.size());
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return;
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), selfMac, 6);
    if (!hasConfirmed(mac.data())) {
        confirmedSet_.push_back(mac);
    }
    if (player_ != nullptr) {
        recordName(selfMac, player_->getName().c_str());
    }
    sendLocalConfirm();
    if (allMembersConfirmed()) {
        LOG_W(TAG, "allMembersConfirmed -> advanceToBracketReveal");
        advanceToBracketReveal();
    }
}

void ShootoutManager::onConfirmReceived(const uint8_t* fromMac, const char* name) {
    if (phase_ != Phase::PROPOSAL) return;
    recordName(fromMac, name);
    bool added = !hasConfirmed(fromMac);
    if (added) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), fromMac, 6);
        confirmedSet_.push_back(mac);
        LOG_W(TAG, "onConfirmReceived from=%s count=%zu",
              MacToString(fromMac), confirmedSet_.size());
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
    for (auto& entry : names_) {
        if (memcmp(entry.mac.data(), mac, 6) == 0) {
            entry.name = buf;
            return;
        }
    }
    NameEntry e;
    memcpy(e.mac.data(), mac, 6);
    e.name = buf;
    names_.push_back(std::move(e));
}

std::string ShootoutManager::getNameForMac(const uint8_t* mac) const {
    for (const auto& entry : names_) {
        if (memcmp(entry.mac.data(), mac, 6) == 0) return entry.name;
    }
    char fallback[4];
    snprintf(fallback, sizeof(fallback), "%02X", mac[5]);
    return fallback;
}

bool ShootoutManager::hasConfirmed(const uint8_t* mac) const {
    for (const auto& existing : confirmedSet_) {
        if (memcmp(existing.data(), mac, 6) == 0) return true;
    }
    return false;
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
    return lowestMacIn(confirmedSet_);
}

bool ShootoutManager::isCoordinator() const {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return false;
    auto coord = lowestMacIn(confirmedSet_);
    return memcmp(coord.data(), selfMac, 6) == 0;
}

void ShootoutManager::generateBracket() {
    bracket_ = confirmedSet_;
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
    std::shuffle(bracket_.begin(), bracket_.end(), rng);
    currentRound_ = bracket_;
}

void ShootoutManager::primeMatchManagerForMatch() {
    if (!matchManager_) return;
    if (!isLocalDuelist()) return;

    // Role-for-this-match from MAC ordering: both sides compute the same
    // ordering so the hunter_draw_time and bounty_time slots in MatchManager
    // are written by exactly one duelist each.
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    bool localIsHunterForMatch = selfMac != nullptr &&
        memcmp(selfMac, opponentMac_.data(), 6) < 0;
    if (player_) player_->setIsHunter(localIsHunterForMatch);

    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    deriveShootoutMatchId(currentMatchIndex_, matchId, sizeof(matchId));
    LOG_W(TAG, "primeMatchManagerForMatch matchIndex=%d localHunter=%d",
          currentMatchIndex_, localIsHunterForMatch);
    matchManager_->initializeShootoutMatch(matchId, opponentMac_.data());
}

void ShootoutManager::advanceToBracketReveal() {
    phase_ = Phase::BRACKET_REVEAL;
    bracketRevealTimer_.setTimer(kBracketRevealMs);
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

std::vector<uint8_t> ShootoutManager::buildBracketPacket() const {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(ShootoutCmd::BRACKET));
    packet.push_back(lastBracketSeqId_);
    packet.push_back(static_cast<uint8_t>(bracket_.size()));
    for (const auto& m : bracket_) {
        packet.insert(packet.end(), m.begin(), m.end());
    }
    return packet;
}

void ShootoutManager::sendBracketToPeers() {
    if (bracket_.empty()) return;
    lastBracketSeqId_ = nextSeqId();
    auto packet = buildBracketPacket();
    sendReliablyToPeers(bracketPendingAcks_, bracket_, packet.data(), packet.size());
}

void ShootoutManager::onBracketAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastBracketSeqId_) return;
    eraseFromPending(bracketPendingAcks_, fromMac);
}

bool ShootoutManager::retryBracketForPeer(BracketPending& p) {
    if (p.retries >= kMaxShootoutAckRetries) {
        // Caller is iterating bracketPendingAcks_; don't abort here (that
        // clears the vector and invalidates the iterator). Signal and let
        // the caller abort after exiting the loop.
        return true;
    }
    auto packet = buildBracketPacket();
    wirelessManager_->sendEspNowData(p.peer.data(), PktType::kShootoutCommand,
                                     packet.data(), packet.size());
    p.retries++;
    p.timer.setTimer(ackTimeoutForRetry(p.retries));
    return false;
}

void ShootoutManager::abortTournament() {
    if (phase_ == Phase::ABORTED) return;
    LOG_W(TAG, "abortTournament from phase=%d", static_cast<int>(phase_));
    phase_ = Phase::ABORTED;
    bracketPendingAcks_.clear();

    // Best-effort: peers that miss ABORT will observe RDC disconnect or their
    // own retry exhaustion and reach ABORTED independently.
    uint8_t packet[2];
    packet[0] = static_cast<uint8_t>(ShootoutCmd::ABORT);
    packet[1] = 0;
    const auto& targets = bracket_.empty() ? confirmedSet_ : bracket_;
    sendToPeers(targets, packet, sizeof(packet));
}

void ShootoutManager::sendLocalConfirm() {
    // [cmd, seq, 6-byte MAC, kNameLength-byte null-padded name]
    uint8_t payload[2 + 6 + kNameLength];
    payload[0] = static_cast<uint8_t>(ShootoutCmd::CONFIRM);
    payload[1] = 0;
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    memcpy(&payload[2], selfMac, 6);
    memset(&payload[8], 0, kNameLength);
    if (player_ != nullptr) {
        const std::string& n = player_->getName();
        size_t copyLen = n.size() < kNameLength ? n.size() : kNameLength;
        memcpy(&payload[8], n.data(), copyLen);
    }

    sendToPeers(getLoopMembers(), payload, sizeof(payload));
    confirmRebroadcastTimer_.setTimer(kConfirmRebroadcastMs);
}

void ShootoutManager::sync() {
    // Check cheap conditions before allMembersConfirmed(), which rebuilds the
    // loop-member set each call.
    if (phase_ == Phase::PROPOSAL && confirmRebroadcastTimer_.expired()) {
        const uint8_t* selfMac = wirelessManager_->getMacAddress();
        if (selfMac != nullptr && hasConfirmed(selfMac) && !allMembersConfirmed()) {
            sendLocalConfirm();
        }
    }

    bool shouldAbort = false;
    for (auto& p : bracketPendingAcks_) {
        if (p.timer.expired()) {
            if (retryBracketForPeer(p)) {
                shouldAbort = true;
                break;
            }
        }
    }
    if (shouldAbort) abortTournament();

    maybeStartNextMatch();

    if (isCoordinator() && phase_ == Phase::MATCH_IN_PROGRESS &&
        matchStartPendingAcks_.empty() &&
        matchStartWatchdog_.expired()) {
        sendMatchStartToPeers(currentMatchIndex_);
    }

    if (phase_ == Phase::ENDED && !tournamentEndPendingAcks_.empty()) {
        uint8_t packet[8];
        packet[0] = static_cast<uint8_t>(ShootoutCmd::TOURNAMENT_END);
        packet[1] = lastTournamentEndSeqId_;
        memcpy(&packet[2], tournamentWinner_.data(), 6);
        for (auto it = tournamentEndPendingAcks_.begin();
             it != tournamentEndPendingAcks_.end(); ) {
            if (!it->timer.expired()) { ++it; continue; }
            if (it->retries >= kMaxShootoutAckRetries) {
                LOG_W(TAG, "TOURNAMENT_END retries exhausted for %s",
                      MacToString(it->peer.data()));
                it = tournamentEndPendingAcks_.erase(it);
                continue;
            }
            wirelessManager_->sendEspNowData(it->peer.data(),
                                             PktType::kShootoutCommand,
                                             packet, sizeof(packet));
            it->retries++;
            it->timer.setTimer(ackTimeoutForRetry(it->retries));
            ++it;
        }
    }

    if (!matchResultPendingAcks_.empty()) {
        auto packet = buildMatchResultPacket(
            lastMatchResult_.winner.data(), lastMatchResult_.loser.data(),
            lastMatchResult_.matchIndex);
        for (auto it = matchResultPendingAcks_.begin();
             it != matchResultPendingAcks_.end(); ) {
            if (!it->timer.expired()) { ++it; continue; }
            if (it->retries >= kMaxShootoutAckRetries) {
                LOG_W(TAG, "MATCH_RESULT retries exhausted for %s",
                      MacToString(it->peer.data()));
                it = matchResultPendingAcks_.erase(it);
                continue;
            }
            wirelessManager_->sendEspNowData(it->peer.data(),
                                             PktType::kShootoutCommand,
                                             packet.data(), packet.size());
            it->retries++;
            it->timer.setTimer(ackTimeoutForRetry(it->retries));
            ++it;
        }
    }
}

std::pair<std::array<uint8_t,6>, std::array<uint8_t,6>>
ShootoutManager::getCurrentMatchPair() const {
    if (currentMatchIndex_ < 0) return {};
    return {currentDuelistA_, currentDuelistB_};
}

std::vector<uint8_t> ShootoutManager::buildMatchStartPacket(int matchIndex) const {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(ShootoutCmd::MATCH_START));
    packet.push_back(lastMatchStartSeqId_);
    const auto& a = currentRound_[matchIndex * 2];
    const auto& b = currentRound_[matchIndex * 2 + 1];
    packet.insert(packet.end(), a.begin(), a.end());
    packet.insert(packet.end(), b.begin(), b.end());
    packet.push_back(static_cast<uint8_t>(matchIndex));
    return packet;
}

void ShootoutManager::sendMatchStartToPeers(int matchIndex) {
    lastMatchStartSeqId_ = nextSeqId();

    auto packet = buildMatchStartPacket(matchIndex);
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    const auto& a = currentRound_[matchIndex * 2];
    const auto& b = currentRound_[matchIndex * 2 + 1];
    currentDuelistA_ = a;
    currentDuelistB_ = b;
    currentMatchIndex_ = matchIndex;
    if (isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, a.data(), 6) == 0) ? b.data() : a.data();
        memcpy(opponentMac_.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    sendReliablyToPeers(matchStartPendingAcks_, bracket_, packet.data(), packet.size());
    matchStartWatchdog_.setTimer(kMatchWatchdogMs);
}

void ShootoutManager::onMatchStartAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastMatchStartSeqId_) return;
    eraseFromPending(matchStartPendingAcks_, fromMac);
}

bool ShootoutManager::isActiveDuelist(const uint8_t* mac) const {
    if (currentMatchIndex_ < 0) return false;
    auto pair = getCurrentMatchPair();
    return memcmp(pair.first.data(), mac, 6) == 0 ||
           memcmp(pair.second.data(), mac, 6) == 0;
}

bool ShootoutManager::isForfeited(const uint8_t* mac) const {
    for (const auto& m : forfeited_) {
        if (memcmp(m.data(), mac, 6) == 0) return true;
    }
    return false;
}

bool ShootoutManager::isEliminatedOrForfeited(const uint8_t* mac) const {
    return isEliminated(mac) || isForfeited(mac);
}

void ShootoutManager::onLocalRDCDisconnect(const uint8_t* lostMac) {
    LOG_W(TAG, "onLocalRDCDisconnect %s phase=%d",
          MacToString(lostMac), static_cast<int>(phase_));
    uint8_t packet[8];
    packet[0] = static_cast<uint8_t>(ShootoutCmd::PEER_LOST);
    packet[1] = 0;
    memcpy(&packet[2], lostMac, 6);
    const auto& targets = bracket_.empty() ? confirmedSet_ : bracket_;
    sendToPeers(targets, packet, sizeof(packet));
    onPeerLostReceived(lostMac);
}

void ShootoutManager::onPeerLostReceived(const uint8_t* lostMac) {
    LOG_W(TAG, "onPeerLostReceived %s phase=%d",
          MacToString(lostMac), static_cast<int>(phase_));
    if (phase_ == Phase::IDLE || phase_ == Phase::ABORTED || phase_ == Phase::ENDED) return;

    auto coord = getCoordinatorMac();
    if (memcmp(coord.data(), lostMac, 6) == 0) {
        LOG_W(TAG, "coordinator lost -> abortTournament");
        abortTournament();
        return;
    }

    if (isEliminatedOrForfeited(lostMac)) return;

    if (phase_ == Phase::MATCH_IN_PROGRESS && isActiveDuelist(lostMac)) {
        auto pair = getCurrentMatchPair();
        const uint8_t* selfMac = wirelessManager_->getMacAddress();
        const uint8_t* winner = (memcmp(pair.first.data(), lostMac, 6) == 0)
            ? pair.second.data() : pair.first.data();
        applyMatchResult(winner, lostMac);
        if (selfMac != nullptr && memcmp(winner, selfMac, 6) == 0) {
            sendMatchResultToPeers(winner, lostMac,
                                 static_cast<uint8_t>(currentMatchIndex_));
        }
        if (isCoordinator()) maybeStartNextMatch();
        return;
    }

    // Non-eliminated spectator: mark forfeited.
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), lostMac, 6);
    forfeited_.push_back(mac);
}

void ShootoutManager::maybeStartNextMatch() {
    if (!isCoordinator()) return;
    if (!bracketPendingAcks_.empty()) return;
    if (phase_ != Phase::BRACKET_REVEAL && phase_ != Phase::BETWEEN_MATCHES) return;
    if (phase_ == Phase::BRACKET_REVEAL && !bracketRevealTimer_.expired()) return;
    // Re-entrancy guard: this function mutates currentMatchIndex_, bracket_,
    // and phase_; concurrent entry from sync() and ESP-NOW recv callbacks
    // (Core 0 vs main loop) would double-advance the bracket.
    if (inMaybeStartNextMatch_) return;
    inMaybeStartNextMatch_ = true;
    struct Guard { bool& f; ~Guard() { f = false; } } guard{inMaybeStartNextMatch_};

    while (true) {
        currentMatchIndex_++;
        int pairEnd = currentMatchIndex_ * 2 + 1;
        if (pairEnd >= (int)currentRound_.size()) {
            std::vector<std::array<uint8_t, 6>> survivors;
            for (const auto& m : currentRound_) {
                if (!isEliminatedOrForfeited(m.data())) survivors.push_back(m);
            }
            if (survivors.size() <= 1) {
                auto winner = survivors.empty()
                    ? findLastRemaining()
                    : survivors[0];
                sendTournamentEndToPeers(winner.data());
                return;
            }
            LOG_W(TAG, "advancing round: %zu survivors -> %zu",
                  currentRound_.size(), survivors.size());
            currentRound_ = survivors;
            currentMatchIndex_ = 0;
            pairEnd = 1;
        }
        const auto& a = currentRound_[currentMatchIndex_ * 2];
        const auto& b = currentRound_[currentMatchIndex_ * 2 + 1];
        bool aForfeit = isForfeited(a.data());
        bool bForfeit = isForfeited(b.data());
        if (aForfeit && bForfeit) continue;
        if (aForfeit) {
            sendMatchResultToPeers(b.data(), a.data(), static_cast<uint8_t>(currentMatchIndex_));
            applyMatchResult(b.data(), a.data());
            continue;
        }
        if (bForfeit) {
            sendMatchResultToPeers(a.data(), b.data(), static_cast<uint8_t>(currentMatchIndex_));
            applyMatchResult(a.data(), b.data());
            continue;
        }
        sendMatchStartToPeers(currentMatchIndex_);
        phase_ = Phase::MATCH_IN_PROGRESS;
        return;
    }
}

std::array<uint8_t, 6> ShootoutManager::lowestMacIn(
    const std::vector<std::array<uint8_t, 6>>& set) {
    std::array<uint8_t, 6> lowest = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (const auto& m : set) {
        if (memcmp(m.data(), lowest.data(), 6) < 0) lowest = m;
    }
    return lowest;
}

void ShootoutManager::onBracketReceived(
    const std::vector<std::array<uint8_t, 6>>& bracket, uint8_t seqId) {
    if (isCoordinator()) return;
    if (seqId != 0 && seqId == lastObservedBracketSeqId_) {
        auto coord = lowestMacIn(bracket);
        sendShootoutAck(ShootoutCmd::BRACKET, seqId, coord.data());
        return;
    }
    lastObservedBracketSeqId_ = seqId;
    bracket_ = bracket;
    currentRound_ = bracket;
    phase_ = Phase::BRACKET_REVEAL;
    bracketRevealTimer_.setTimer(kBracketRevealMs);
    auto coord = lowestMacIn(bracket);
    sendShootoutAck(ShootoutCmd::BRACKET, seqId, coord.data());
}

void ShootoutManager::onMatchStartReceived(
    const uint8_t* duelistA, const uint8_t* duelistB,
    uint8_t matchIndex, uint8_t seqId) {
    // Coordinator broadcasts MATCH_START to others; it should never
    // receive its own. Guard is symmetric with onBracketReceived.
    if (isCoordinator()) return;
    if (seqId != 0 && seqId == lastObservedMatchStartSeqId_) {
        auto coord = lowestMacIn(bracket_);
        sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coord.data());
        return;
    }
    lastObservedMatchStartSeqId_ = seqId;
    currentMatchIndex_ = matchIndex;
    memcpy(currentDuelistA_.data(), duelistA, 6);
    memcpy(currentDuelistB_.data(), duelistB, 6);
    phase_ = Phase::MATCH_IN_PROGRESS;
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, duelistA, 6) == 0) ? duelistB : duelistA;
        memcpy(opponentMac_.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    auto coord = lowestMacIn(bracket_);
    sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coord.data());
}

void ShootoutManager::sendShootoutAck(ShootoutCmd cmd, uint8_t seqId, const uint8_t* toMac) {
    ShootoutAckPayload ack{cmd, seqId};
    wirelessManager_->sendEspNowData(toMac, PktType::kShootoutCommandAck,
                                     reinterpret_cast<uint8_t*>(&ack), sizeof(ack));
}

bool ShootoutManager::isEliminated(const uint8_t* mac) const {
    for (const auto& m : eliminated_) {
        if (memcmp(m.data(), mac, 6) == 0) return true;
    }
    return false;
}

void ShootoutManager::applyMatchResult(const uint8_t* winner, const uint8_t* loser) {
    if (!isEliminated(loser)) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), loser, 6);
        eliminated_.push_back(mac);
    }
    phase_ = Phase::BETWEEN_MATCHES;
    matchStartWatchdog_.invalidate();
}

std::vector<uint8_t> ShootoutManager::buildMatchResultPacket(
    const uint8_t* winner, const uint8_t* loser, uint8_t matchIndex) const {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(ShootoutCmd::MATCH_RESULT));
    packet.push_back(lastMatchResultSeqId_);
    packet.insert(packet.end(), winner, winner + 6);
    packet.insert(packet.end(), loser, loser + 6);
    packet.push_back(matchIndex);
    return packet;
}

void ShootoutManager::sendMatchResultToPeers(
    const uint8_t* winner, const uint8_t* loser, uint8_t matchIndex) {
    lastMatchResultSeqId_ = nextSeqId();
    memcpy(lastMatchResult_.winner.data(), winner, 6);
    memcpy(lastMatchResult_.loser.data(), loser, 6);
    lastMatchResult_.matchIndex = matchIndex;
    auto packet = buildMatchResultPacket(winner, loser, matchIndex);
    // Targets confirmedSet_ to reach already-eliminated players too.
    sendReliablyToPeers(matchResultPendingAcks_, confirmedSet_, packet.data(), packet.size());
}

void ShootoutManager::reportLocalWin() {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return;
    LOG_W(TAG, "reportLocalWin matchIndex=%d", currentMatchIndex_);
    sendMatchResultToPeers(selfMac, opponentMac_.data(), static_cast<uint8_t>(currentMatchIndex_));
    applyMatchResult(selfMac, opponentMac_.data());
    if (isCoordinator()) maybeStartNextMatch();
}

void ShootoutManager::onMatchResultReceived(
    const uint8_t* winner, const uint8_t* loser,
    uint8_t matchIndex, uint8_t seqId, const uint8_t* fromMac) {
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
    if (seqId != lastMatchResultSeqId_) return;
    eraseFromPending(matchResultPendingAcks_, fromMac);
}

size_t ShootoutManager::getMatchResultPendingAckCount() const {
    return matchResultPendingAcks_.size();
}

std::array<uint8_t, 6> ShootoutManager::findLastRemaining() const {
    for (const auto& m : bracket_) {
        if (!isEliminatedOrForfeited(m.data())) return m;
    }
    return {};
}

void ShootoutManager::sendTournamentEndToPeers(const uint8_t* winner) {
    LOG_W(TAG, "tournamentEnd winner=%s", MacToString(winner));
    lastTournamentEndSeqId_ = nextSeqId();
    uint8_t packet[8];
    packet[0] = static_cast<uint8_t>(ShootoutCmd::TOURNAMENT_END);
    packet[1] = lastTournamentEndSeqId_;
    memcpy(&packet[2], winner, 6);
    // Targets confirmedSet_ rather than bracket_: eliminated players need the
    // tournament-end transition or they stall in BETWEEN_MATCHES.
    sendReliablyToPeers(tournamentEndPendingAcks_, confirmedSet_, packet, sizeof(packet));
    memcpy(tournamentWinner_.data(), winner, 6);
    phase_ = Phase::ENDED;
}

void ShootoutManager::onTournamentEndAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastTournamentEndSeqId_) return;
    eraseFromPending(tournamentEndPendingAcks_, fromMac);
}

void ShootoutManager::onTournamentEndReceived(const uint8_t* winner, uint8_t seqId) {
    if (seqId != 0 && seqId == lastObservedTournamentEndSeqId_) {
        auto coord = getCoordinatorMac();
        sendShootoutAck(ShootoutCmd::TOURNAMENT_END, seqId, coord.data());
        return;
    }
    lastObservedTournamentEndSeqId_ = seqId;
    memcpy(tournamentWinner_.data(), winner, 6);
    phase_ = Phase::ENDED;
    auto coord = getCoordinatorMac();
    sendShootoutAck(ShootoutCmd::TOURNAMENT_END, seqId, coord.data());
}

void ShootoutManager::onAbortReceived() {
    if (phase_ == Phase::ABORTED || phase_ == Phase::IDLE) return;
    phase_ = Phase::ABORTED;
    bracketPendingAcks_.clear();
    matchStartPendingAcks_.clear();
}

std::vector<std::array<uint8_t, 6>> ShootoutManager::buildLoopMemberSet() const {
    if (!cdm_->isLoop()) return {};

    std::vector<std::array<uint8_t, 6>> out;
    auto addUnique = [&out](const uint8_t* mac) {
        if (mac == nullptr) return;
        for (const auto& existing : out) {
            if (memcmp(existing.data(), mac, 6) == 0) return;
        }
        std::array<uint8_t, 6> copy;
        memcpy(copy.data(), mac, 6);
        out.push_back(copy);
    };

    addUnique(wirelessManager_->getMacAddress());
    addUnique(rdc_->getPeerMac(SerialIdentifier::OUTPUT_JACK));
    addUnique(rdc_->getPeerMac(SerialIdentifier::INPUT_JACK));

    for (auto jack : {SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK}) {
        PortState state = rdc_->getPortState(jack);
        for (const auto& peer : state.peerMacAddresses) {
            addUnique(peer.data());
        }
    }

    return out;
}
