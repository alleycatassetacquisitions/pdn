#include "game/game-session.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/logger.hpp"
#include <array>
#include <cstring>

const std::array<GameSession::PacketRoute, 6>& GameSession::packetRoutes() {
    // kRoleAnnounce is absent deliberately: ChainDuelManager's ReliableChannel
    // claims that slot itself. Installing it here too would clobber the channel,
    // since this loop runs after the managers are constructed.
    static const std::array<PacketRoute, 6> ROUTES = {{
        {PktType::kChainGameEvent, dispatchTo<&GameSession::onChainGameEventPacket>},
        {PktType::kChainGameEventAck, dispatchTo<&GameSession::onChainGameEventAckPacket>},
        {PktType::kChainConfirm, dispatchTo<&GameSession::onChainConfirmPacket>},
        {PktType::kChainJoin, dispatchTo<&GameSession::onChainJoinPacket>},
        {PktType::kShootoutCommand, dispatchTo<&GameSession::onShootoutCommandPacket>},
        {PktType::kShootoutCommandAck, dispatchTo<&GameSession::onShootoutCommandAckPacket>},
    }};
    return ROUTES;
}

// The initializer list is in declaration order, which is also dependency order:
// both managers read the two device-owned managers pulled off the PDN above them.
GameSession::GameSession(Player* player,
                         Device* pdn,
                         QuickdrawWirelessManager* quickdrawWirelessManager,
                         SymbolWirelessManager* symbolWirelessManager)
    : player(player)
    , pdn(pdn)
    , wirelessManager(pdn->getWirelessManager())
    , remoteDeviceCoordinator(pdn->getRemoteDeviceCoordinator())
    , quickdrawWirelessManager(quickdrawWirelessManager)
    , symbolWirelessManager(symbolWirelessManager)
    , matchManager(new MatchManager())
    , chainDuelManager(new ChainDuelManager(player, wirelessManager, remoteDeviceCoordinator))
    , shootoutManager(new ShootoutManager(player, wirelessManager, remoteDeviceCoordinator)) {
    this->shootoutManager->setMatchManager(matchManager);
    matchManager->setShootoutManager(shootoutManager);

    matchManager->initialize(player, pdn->getStorage(), quickdrawWirelessManager);
    matchManager->setBoostProvider([this]() -> unsigned long {
        return chainDuelManager ? chainDuelManager->getBoostMs() : 0;
    });
    matchManager->setRemoteDeviceCoordinator(remoteDeviceCoordinator);

    quickdrawWirelessManager->setPacketReceivedCallback(
        [this](const QuickdrawCommand& command) { matchManager->listenForMatchEvents(command); });

    for (const PacketRoute& route : packetRoutes()) {
        wirelessManager->setEspNowPacketHandler(route.type, route.handler, this);
    }

    // The Player is the authority on this device's identity; RDC only carries it.
    remoteDeviceCoordinator->setSelfProfileProvider([this]() -> PlayerProfile {
        return this->player->toProfile();
    });

    // Registration flips hunter/bounty long after the jacks came up, and the
    // context exchange only runs on connect, so the flip has to push itself.
    this->player->setOnRoleChanged([this]() {
        remoteDeviceCoordinator->resendContext();
    });

    // Installed here rather than by the caller so the slot holding `this` is
    // dropped by the same destructor that drops the rest of them.
    pdn->setTickCallback([this]() { sync(); });
}

GameSession::~GameSession() {
    // Every slot below holds `this` — some by lambda capture, the packet handlers as
    // a void* ctx — on an object that outlives this session, so each must be dropped
    // before that pointer dangles.
    if (pdn) pdn->setTickCallback(nullptr);
    pdn = nullptr;
    if (player) player->setOnRoleChanged(nullptr);
    if (remoteDeviceCoordinator) remoteDeviceCoordinator->setSelfProfileProvider(nullptr);
    player = nullptr;
    // setBoostProvider is the one install with no clear: matchManager owns that slot
    // and is deleted below.
    remoteDeviceCoordinator = nullptr;
    for (const PacketRoute& route : packetRoutes()) {
        wirelessManager->clearEspNowPacketHandler(route.type);
    }
    if (quickdrawWirelessManager) {
        quickdrawWirelessManager->clearCallbacks();
    }
    quickdrawWirelessManager = nullptr;
    symbolWirelessManager = nullptr;
    // Managers before matchManager: shootoutManager holds a raw MatchManager*
    // and dereferences it when priming a bracket match.
    delete chainDuelManager;
    chainDuelManager = nullptr;
    delete shootoutManager;
    shootoutManager = nullptr;
    delete matchManager;
    matchManager = nullptr;
}

GameContext GameSession::getContext() {
    GameContext context;
    context.player = player;
    context.matchManager = matchManager;
    context.remoteDeviceCoordinator = remoteDeviceCoordinator;
    context.chainDuelManager = chainDuelManager;
    context.shootoutManager = shootoutManager;
    context.quickdrawWirelessManager = quickdrawWirelessManager;
    context.symbolWirelessManager = symbolWirelessManager;
    context.wirelessManager = wirelessManager;
    return context;
}

SupporterReady* GameSession::getMountedSupporterReady() {
    if (pdn == nullptr) return nullptr;
    StateMachine* activeApp = pdn->getActiveApp();
    if (activeApp == nullptr) return nullptr;
    State* current = activeApp->getCurrentState();
    if (current == nullptr || current->getStateId() != SUPPORTER_READY) return nullptr;
    return static_cast<SupporterReady*>(current);
}

void GameSession::sync() {
    if (chainDuelManager) {
        chainDuelManager->sync();

        bool loopNow = chainDuelManager->isLoop();
        if (loopNow != lastIsLoop) {
            LOG_W("CDM", "isLoop %d -> %d", (int)lastIsLoop, (int)loopNow);
            lastIsLoop = loopNow;
        }
    }
    // Driven here rather than from the shootout states so a tournament's own
    // retries keep running while the device is in the duel app for a bracket
    // match.
    if (shootoutManager) shootoutManager->sync();

    logRetryStats();
}

void GameSession::logRetryStats() {
    if (!statsLogTimer.isRunning()) {
        statsLogTimer.setTimer(STATS_LOG_INTERVAL_MS);
        return;
    }
    if (!statsLogTimer.expired()) return;

    // LOG_W (not LOG_I) because firmware builds with CORE_DEBUG_LEVEL=2, which
    // strips info-level calls. Both managers are reported: a venue reading one
    // line to judge radio health would otherwise be shown the chain duel's
    // retries and told nothing about the tournament's.
    if (chainDuelManager != nullptr) {
        ChainDuelManager::RetryStats c = chainDuelManager->getRetryStats();
        unsigned long cMean = c.ackCount ? (c.ackLatencyMsSum / c.ackCount) : 0;
        LOG_W("STATS", "CDM s=%u r=%u ab=%u ack=%u/%lums",
              (unsigned)c.sends, (unsigned)c.retries, (unsigned)c.abandons,
              (unsigned)c.ackCount, cMean);
    }
    if (shootoutManager != nullptr) {
        const Resender::Stats& s = shootoutManager->getRetryStats();
        LOG_W("STATS", "SHT s=%u r=%u ab=%u",
              (unsigned)s.sends, (unsigned)s.retries, (unsigned)s.abandons);
    }
    statsLogTimer.setTimer(STATS_LOG_INTERVAL_MS);
}

void GameSession::onChainGameEventPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(ChainGameEventPayload)) return;
    if (!chainDuelManager) return;

    const ChainGameEventPayload* payload = reinterpret_cast<const ChainGameEventPayload*>(data);
    // The frame is broadcast, so every chain in radio range hears it. The
    // champion it names is the only thing that says whether it is ours.
    if (!chainDuelManager->isEventFromOwnChampion(payload->championMac)) return;

    // Ahead of the state dispatch and independent of it: a COUNTDOWN wipes the
    // champion's roll call whether or not this device is watching for it, and a
    // confirm still held here would then re-register a press for a round the
    // supporter has not answered.
    chainDuelManager->onChainGameEventReceived(payload->event_type);

    // Out-of-state events dropped silently; champion's retry machine bounds traffic cost.
    SupporterReady* supporterReady = getMountedSupporterReady();
    if (supporterReady != nullptr) {
        supporterReady->onChainGameEventReceived(payload->event_type, fromMac);
    }

    // ACK regardless of whether we were in SupporterReady. Not ACKing after
    // leaving the state would let the champion keep retrying a WIN/LOSS we
    // already received and can't act on. seqId=0 is the sentinel for
    // fire-and-forget events (COUNTDOWN/DRAW) and must not be ACKed.
    if (payload->seqId != 0) {
        chainDuelManager->sendGameEventAck(fromMac, payload->seqId);
    }
}

void GameSession::onChainGameEventAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(ChainGameEventAckPayload) || !chainDuelManager) return;
    const ChainGameEventAckPayload* payload = reinterpret_cast<const ChainGameEventAckPayload*>(data);
    chainDuelManager->onChainGameEventAckReceived(fromMac, payload->seqId);
}

void GameSession::onChainConfirmPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(ChainConfirmPayload)) return;
    if (!chainDuelManager) return;
    // Deliberately ungated on the sender: a confirm is unicast straight to the
    // champion from any depth, so the sender is usually a device we share no
    // cable with and no adjacency test can recognise it. Membership is decided
    // against the join roster when the count is read, and a press whose join has
    // not landed yet is held rather than dropped.
    const ChainConfirmPayload* payload = reinterpret_cast<const ChainConfirmPayload*>(data);
    chainDuelManager->onConfirmReceived(fromMac, payload->originatorMac, payload->seqId);
}

void GameSession::onChainJoinPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(ChainJoinPayload) || !chainDuelManager) return;
    const ChainJoinPayload* payload = reinterpret_cast<const ChainJoinPayload*>(data);
    chainDuelManager->onChainJoinReceived(fromMac, payload->championMac);
}

namespace {
// [count, count * 6-byte MAC] — the body BRACKET and RING_CLOSED share.
bool decodeMacList(const uint8_t* payload, size_t payloadLen,
                   std::vector<std::array<uint8_t, 6>>& out) {
    if (payloadLen < 1) return false;
    uint8_t count = payload[0];
    if (count > ShootoutManager::MAX_BRACKET_SIZE) return false;
    if (payloadLen < 1 + 6 * static_cast<size_t>(count)) return false;
    out.reserve(count);
    for (uint8_t i = 0; i < count; i++) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), payload + 1 + 6 * i, 6);
        out.push_back(mac);
    }
    return true;
}
}  // namespace

void GameSession::onShootoutCommandPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (!shootoutManager || dataLen < 2) return;
    if (data[0] > static_cast<uint8_t>(ShootoutCmd::RING_CLOSED)) return;
    ShootoutCmd cmd = static_cast<ShootoutCmd>(data[0]);
    uint8_t seqId = data[1];
    const uint8_t* payload = data + 2;
    size_t payloadLen = dataLen - 2;
    switch (cmd) {
        case ShootoutCmd::CONFIRM: {
            if (payloadLen < 6) break;
            const char* name = (payloadLen >= 6 + ShootoutManager::kNameLength)
                                   ? reinterpret_cast<const char*>(payload + 6)
                                   : nullptr;
            shootoutManager->onConfirmReceived(payload, name);
            break;
        }
        case ShootoutCmd::BRACKET: {
            std::vector<std::array<uint8_t, 6>> bracket;
            if (!decodeMacList(payload, payloadLen, bracket)) break;
            shootoutManager->onBracketReceived(fromMac, bracket, seqId);
            break;
        }
        case ShootoutCmd::RING_CLOSED: {
            std::vector<std::array<uint8_t, 6>> members;
            if (!decodeMacList(payload, payloadLen, members)) break;
            shootoutManager->onRingClosedReceived(fromMac, members);
            break;
        }
        case ShootoutCmd::MATCH_START:
            if (payloadLen >= 13)
                shootoutManager->onMatchStartReceived(payload, payload + 6, payload[12], seqId);
            break;
        case ShootoutCmd::MATCH_RESULT:
            if (payloadLen >= 13)
                shootoutManager->onMatchResultReceived(payload, payload + 6, payload[12], seqId, fromMac);
            break;
        case ShootoutCmd::TOURNAMENT_END:
            if (payloadLen >= 6) shootoutManager->onTournamentEndReceived(payload, seqId);
            break;
        case ShootoutCmd::PEER_LOST:
            if (payloadLen >= 6) shootoutManager->onPeerLostReceived(payload);
            break;
        case ShootoutCmd::ABORT:
            shootoutManager->onAbortReceived(fromMac);
            break;
    }
}

void GameSession::onShootoutCommandAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (!shootoutManager || dataLen < 2) return;
    if (data[0] > static_cast<uint8_t>(ShootoutCmd::ABORT)) return;
    shootoutManager->onCommandAckReceived(fromMac, data[1]);
}
