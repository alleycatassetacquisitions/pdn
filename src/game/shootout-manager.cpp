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
                                 RemoteDeviceCoordinator* rdc,
                                 ChainDuelManager* cdm)
    : player_(player), wirelessManager_(wirelessManager), rdc_(rdc), cdm_(cdm) {
    resender_ = new Resender(wirelessManager);
    resender_->setAbandonCallback(
        [this](PktType type, uint8_t subType, uint8_t /*seqId*/, const uint8_t* /*target*/) {
            if (type != PktType::kShootoutCommand) return;
            if (subType == Resender::toSubType(ShootoutCmd::BRACKET) ||
                subType == Resender::toSubType(ShootoutCmd::MATCH_START) ||
                subType == Resender::toSubType(ShootoutCmd::MATCH_RESULT)) {
                tournamentAbortPending_ = true;
            }
        });
}

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
    return resender_ ? resender_->pendingCount(
        PktType::kShootoutCommand, ShootoutCmd::BRACKET) : 0;
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

std::array<uint8_t, 6> ShootoutManager::getOpponentMac() const {
    return opponentMac_;
}

uint8_t ShootoutManager::getLastMatchStartSeqId() const {
    return lastMatchStartSeqId_;
}

size_t ShootoutManager::getTournamentEndPendingAckCount() const {
    return resender_ ? resender_->pendingCount(
        PktType::kShootoutCommand, ShootoutCmd::TOURNAMENT_END) : 0;
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
    if (resender_) {
        resender_->cancelChannel(PktType::kShootoutCommand, ShootoutCmd::BRACKET);
        resender_->cancelChannel(PktType::kShootoutCommand, ShootoutCmd::MATCH_START);
        resender_->cancelChannel(PktType::kShootoutCommand, ShootoutCmd::MATCH_RESULT);
        resender_->cancelChannel(PktType::kShootoutCommand, ShootoutCmd::TOURNAMENT_END);
    }
    tournamentAbortPending_ = false;
    eliminated_.clear();
    reportedLocalWin_ = false;
    names_.clear();
    lastMatchResult_ = LastMatchResult{};
    lastObservedBracketSeqId_ = 0;
    lastObservedMatchStartSeqId_ = 0;
    lastObservedTournamentEndSeqId_ = 0;
    currentMatchIndex_ = -1;
    memset(tournamentWinner_.data(), 0, 6);
    memset(opponentMac_.data(), 0, 6);
    memset(currentDuelistA_.data(), 0, 6);
    memset(currentDuelistB_.data(), 0, 6);
    memset(coordinatorMac_.data(), 0, 6);
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
    // Fast path: already-confirmed peers bypass the loop-membership scan (this
    // is the common case during 1Hz rebroadcasts — the gate only needs to
    // block first-time stray CONFIRMs from outside the ring).
    if (!hasConfirmed(fromMac)) {
        auto members = getLoopMembers();
        bool inLoop = false;
        for (const auto& m : members) {
            if (memcmp(m.data(), fromMac, 6) == 0) { inLoop = true; break; }
        }
        if (!inLoop) return;
    }
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
    if (!bracket_.empty()) return coordinatorMac_;
    return lowestMacIn(confirmedSet_);
}

bool ShootoutManager::isCoordinator() const {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return false;
    auto coord = getCoordinatorMac();
    return memcmp(coord.data(), selfMac, 6) == 0;
}

void ShootoutManager::generateBracket() {
    bracket_ = confirmedSet_;
    coordinatorMac_ = lowestMacIn(bracket_);
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

void ShootoutManager::sendShootoutCommandReliably(
    ShootoutCmd cmd, uint8_t seqId,
    const std::vector<std::array<uint8_t, 6>>& peers,
    const uint8_t* packet, size_t len) {
    if (resender_ == nullptr) return;
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    for (const auto& m : peers) {
        if (selfMac != nullptr && memcmp(m.data(), selfMac, 6) == 0) continue;
        resender_->send(m.data(), PktType::kShootoutCommand,
                        cmd, seqId, packet, len);
    }
}

void ShootoutManager::sendBracketToPeers() {
    if (bracket_.empty()) return;
    lastBracketSeqId_ = nextSeqId();
    auto packet = buildBracketPacket();
    sendShootoutCommandReliably(ShootoutCmd::BRACKET, lastBracketSeqId_,
                                bracket_, packet.data(), packet.size());
}

void ShootoutManager::onBracketAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastBracketSeqId_) return;
    if (resender_) {
        resender_->onAck(PktType::kShootoutCommand,
                         ShootoutCmd::BRACKET, seqId, fromMac);
    }
}

void ShootoutManager::abortTournament() {
    // ENDED is terminal-success; this guard stops a straggling
    // MATCH_RESULT abandon from latching tournamentAbortPending_
    // and demoting the success state.
    if (phase_ == Phase::ABORTED || phase_ == Phase::ENDED) return;
    LOG_W(TAG, "abortTournament from phase=%d", static_cast<int>(phase_));

    // Broadcast before resetToIdle clears bracket_/confirmedSet_.
    uint8_t packet[2];
    packet[0] = static_cast<uint8_t>(ShootoutCmd::ABORT);
    packet[1] = 0;
    const auto& targets = bracket_.empty() ? confirmedSet_ : bracket_;
    sendToPeers(targets, packet, sizeof(packet));

    resetToIdle();
    phase_ = Phase::ABORTED;
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

    if (resender_) resender_->sync();
    if (tournamentAbortPending_) {
        tournamentAbortPending_ = false;
        abortTournament();
    }

    maybeStartNextMatch();
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
    bool sameMatch = isSameMatch(matchIndex, a.data(), b.data());
    currentDuelistA_ = a;
    currentDuelistB_ = b;
    currentMatchIndex_ = matchIndex;
    if (!sameMatch) reportedLocalWin_ = false;
    if (!sameMatch && isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, a.data(), 6) == 0) ? b.data() : a.data();
        memcpy(opponentMac_.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    sendShootoutCommandReliably(ShootoutCmd::MATCH_START, lastMatchStartSeqId_,
                                bracket_, packet.data(), packet.size());
}

void ShootoutManager::onMatchStartAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastMatchStartSeqId_) return;
    if (resender_) {
        resender_->onAck(PktType::kShootoutCommand,
                         ShootoutCmd::MATCH_START, seqId, fromMac);
    }
}

bool ShootoutManager::isSameMatch(int matchIndex, const uint8_t* a, const uint8_t* b) const {
    return matchIndex == currentMatchIndex_
        && phase_ == Phase::MATCH_IN_PROGRESS
        && memcmp(currentDuelistA_.data(), a, 6) == 0
        && memcmp(currentDuelistB_.data(), b, 6) == 0;
}

bool ShootoutManager::isActiveDuelist(const uint8_t* mac) const {
    if (currentMatchIndex_ < 0) return false;
    auto pair = getCurrentMatchPair();
    return memcmp(pair.first.data(), mac, 6) == 0 ||
           memcmp(pair.second.data(), mac, 6) == 0;
}

void ShootoutManager::onLocalRDCDisconnect(const uint8_t* lostMac) {
    LOG_W(TAG, "onLocalRDCDisconnect %s phase=%d",
          MacToString(lostMac), static_cast<int>(phase_));
    if (phase_ == Phase::IDLE || phase_ == Phase::ABORTED || phase_ == Phase::ENDED) return;
    if (rdc_ && rdc_->canReachPeer(lostMac)) return;
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
    if (rdc_ && rdc_->canReachPeer(lostMac)) return;
    abortTournament();
}

void ShootoutManager::maybeStartNextMatch() {
    if (!isCoordinator()) return;
    if (getBracketPendingAckCount() != 0) return;
    if (phase_ != Phase::BRACKET_REVEAL && phase_ != Phase::BETWEEN_MATCHES) return;
    if (phase_ == Phase::BRACKET_REVEAL && !bracketRevealTimer_.expired()) return;
    // Re-entrancy guard: reportLocalWin calls maybeStartNextMatch directly,
    // and the same sync() tick can then re-enter through the match-advance
    // path. Both run on the main loop (ESP-NOW packets land in the drain
    // queue and dispatch from sync()), so this guards same-tick recursion,
    // not cross-thread concurrency.
    if (inMaybeStartNextMatch_) return;
    inMaybeStartNextMatch_ = true;
    struct Guard { bool& f; ~Guard() { f = false; } } guard{inMaybeStartNextMatch_};

    currentMatchIndex_++;
    int pairEnd = currentMatchIndex_ * 2 + 1;
    if (pairEnd >= (int)currentRound_.size()) {
        std::vector<std::array<uint8_t, 6>> survivors;
        for (const auto& m : currentRound_) {
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
              currentRound_.size(), survivors.size());
        currentRound_ = survivors;
        currentMatchIndex_ = 0;
    }
    sendMatchStartToPeers(currentMatchIndex_);
    phase_ = Phase::MATCH_IN_PROGRESS;
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
    coordinatorMac_ = lowestMacIn(bracket_);
    phase_ = Phase::BRACKET_REVEAL;
    bracketRevealTimer_.setTimer(kBracketRevealMs);
    sendShootoutAck(ShootoutCmd::BRACKET, seqId, coordinatorMac_.data());
}

void ShootoutManager::onMatchStartReceived(
    const uint8_t* duelistA, const uint8_t* duelistB,
    uint8_t matchIndex, uint8_t seqId) {
    if (isCoordinator()) return;
    if (seqId != 0 && seqId == lastObservedMatchStartSeqId_) {
        sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coordinatorMac_.data());
        return;
    }
    bool sameMatch = isSameMatch(matchIndex, duelistA, duelistB);
    lastObservedMatchStartSeqId_ = seqId;
    if (sameMatch) {
        sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coordinatorMac_.data());
        return;
    }
    currentMatchIndex_ = matchIndex;
    memcpy(currentDuelistA_.data(), duelistA, 6);
    memcpy(currentDuelistB_.data(), duelistB, 6);
    phase_ = Phase::MATCH_IN_PROGRESS;
    reportedLocalWin_ = false;
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (isLocalDuelist() && selfMac != nullptr) {
        const uint8_t* opp = (memcmp(selfMac, duelistA, 6) == 0) ? duelistB : duelistA;
        memcpy(opponentMac_.data(), opp, 6);
        primeMatchManagerForMatch();
    }
    sendShootoutAck(ShootoutCmd::MATCH_START, seqId, coordinatorMac_.data());
}

void ShootoutManager::sendShootoutAck(ShootoutCmd cmd, uint8_t seqId, const uint8_t* toMac) {
    Resender::sendAck(wirelessManager_, toMac, PktType::kShootoutCommand, cmd, seqId);
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
    // Restore pre-tournament role at each match boundary. primeMatchManagerForMatch
    // re-applies the per-match override on the next match start if this device is a
    // duelist again. Prevents role from staying flipped when the tournament ends.
    if (originalIsHunter_ && player_) {
        player_->setIsHunter(*originalIsHunter_);
    }
    phase_ = Phase::BETWEEN_MATCHES;
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
    sendShootoutCommandReliably(ShootoutCmd::MATCH_RESULT, lastMatchResultSeqId_,
                                confirmedSet_, packet.data(), packet.size());
}

void ShootoutManager::reportLocalWin() {
    const uint8_t* selfMac = wirelessManager_->getMacAddress();
    if (selfMac == nullptr) return;
    if (reportedLocalWin_) return;
    reportedLocalWin_ = true;
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
    if (resender_) {
        resender_->onAck(PktType::kShootoutCommand,
                         ShootoutCmd::MATCH_RESULT, seqId, fromMac);
    }
}

size_t ShootoutManager::getMatchResultPendingAckCount() const {
    return resender_ ? resender_->pendingCount(
        PktType::kShootoutCommand, ShootoutCmd::MATCH_RESULT) : 0;
}

std::array<uint8_t, 6> ShootoutManager::findLastRemaining() const {
    for (const auto& m : bracket_) {
        if (!isEliminated(m.data())) return m;
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
    sendShootoutCommandReliably(ShootoutCmd::TOURNAMENT_END, lastTournamentEndSeqId_,
                                confirmedSet_, packet, sizeof(packet));
    memcpy(tournamentWinner_.data(), winner, 6);
    phase_ = Phase::ENDED;
    // Drop any in-flight MATCH_RESULT pending so a straggling abandon does
    // not latch tournamentAbortPending_ after the tournament has ended.
    if (resender_) {
        resender_->cancelChannel(PktType::kShootoutCommand, ShootoutCmd::MATCH_RESULT);
    }
}

void ShootoutManager::onTournamentEndAckReceived(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId != lastTournamentEndSeqId_) return;
    if (resender_) {
        resender_->onAck(PktType::kShootoutCommand,
                         ShootoutCmd::TOURNAMENT_END, seqId, fromMac);
    }
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
    // Drop any in-flight MATCH_RESULT pending so a straggling abandon does
    // not latch tournamentAbortPending_ after the tournament has ended.
    if (resender_) {
        resender_->cancelChannel(PktType::kShootoutCommand, ShootoutCmd::MATCH_RESULT);
    }
    auto coord = getCoordinatorMac();
    sendShootoutAck(ShootoutCmd::TOURNAMENT_END, seqId, coord.data());
}

void ShootoutManager::onAbortReceived() {
    // ENDED is terminal-success and must not be demoted by a stray ABORT
    // from a peer that diverged.
    if (phase_ == Phase::ABORTED || phase_ == Phase::IDLE || phase_ == Phase::ENDED) return;
    resetToIdle();
    phase_ = Phase::ABORTED;
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
