#include "game/game-session.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/reliable-transport.hpp"
#include <array>
#include <cstring>

const std::array<GameSession::PacketRoute, 2>& GameSession::packetRoutes() {
    // Channel-owned PktTypes are omitted; registering them here after manager construction would clobber their channels.
    static const std::array<PacketRoute, 2> ROUTES = {{
    {PktType::kShootoutCommand, dispatchTo<&GameSession::onShootoutCommandPacket>},
    {PktType::kShootoutCommandAck, dispatchTo<&GameSession::onShootoutCommandAckPacket>},
    }};
    return ROUTES;
}

// The initializer list is in declaration order, which is also dependency order:
// both managers read the two device-owned managers pulled off the PDN above them.
GameSession::GameSession(Player* player,
                         Device* pdn,
                         SymbolWirelessManager* symbolWirelessManager)
    : player(player)
    , pdn(pdn)
    , wirelessManager(pdn->getWirelessManager())
    , remoteDeviceCoordinator(pdn->getRemoteDeviceCoordinator())
    , symbolWirelessManager(symbolWirelessManager)
    , matchManager(new MatchManager(wirelessManager))
    , posseManager(new PosseManager(player, wirelessManager, remoteDeviceCoordinator))
    , shootoutManager(new ShootoutManager(player, wirelessManager, remoteDeviceCoordinator)) {
    this->shootoutManager->setMatchManager(matchManager);
    matchManager->setShootoutManager(shootoutManager);

    matchManager->initialize(player, pdn->getStorage());
    matchManager->setBoostProvider([this]() -> unsigned long {
        return posseManager ? posseManager->getBoostMs() : 0;
    });
    matchManager->setRemoteDeviceCoordinator(remoteDeviceCoordinator);

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
        posseManager->onChainStateChanged();
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
    symbolWirelessManager = nullptr;
    // Managers before matchManager: shootoutManager holds a raw MatchManager*
    // and dereferences it when priming a bracket match.
    delete posseManager;
    posseManager = nullptr;
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
    context.posseManager = posseManager;
    context.shootoutManager = shootoutManager;
    context.symbolWirelessManager = symbolWirelessManager;
    return context;
}

void GameSession::sync() {
    if (posseManager) {
        posseManager->sync();

        bool inRingNow = remoteDeviceCoordinator->isInRing();
        if (inRingNow != lastIsInRing) {
            LOG_W("RDC", "isInRing %d -> %d", (int)lastIsInRing, (int)inRingNow);
            lastIsInRing = inRingNow;
        }
    }
    // Driven here rather than from the shootout states so a tournament's own
    // retries keep running while the device is in the match app for a bracket
    // match.
    if (shootoutManager) shootoutManager->sync();
    if (matchManager) matchManager->sync();

    logRetryStats();
}

void GameSession::logRetryStats() {
    if (!statsLogTimer.isRunning()) {
        statsLogTimer.setTimer(STATS_LOG_INTERVAL_MS);
        return;
    }
    if (!statsLogTimer.expired()) return;

    // LOG_W (not LOG_I) because firmware builds with CORE_DEBUG_LEVEL=2, which
    // strips info-level calls. The tournament's own queue is reported beside
    // the device transport's so one line judges radio health.
    const Resender::Stats& t = wirelessManager->getReliableTransport()->getStats();
    LOG_W("STATS", "RELIABLE s=%u r=%u ab=%u", (unsigned)t.sends, (unsigned)t.retries,
          (unsigned)t.abandons);
    if (posseManager != nullptr) {
        PosseManager::AckLatency latency = posseManager->getAckLatency();
        unsigned long mean = latency.count ? (latency.sumMs / latency.count) : 0;
        LOG_W("STATS", "POSSE ack=%u/%lums", (unsigned)latency.count, mean);
    }
    if (shootoutManager != nullptr) {
        const Resender::Stats& s = shootoutManager->getRetryStats();
        LOG_W("STATS", "SHT s=%u r=%u ab=%u",
              (unsigned)s.sends, (unsigned)s.retries, (unsigned)s.abandons);
    }
    statsLogTimer.setTimer(STATS_LOG_INTERVAL_MS);
}

void GameSession::onShootoutCommandPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (!shootoutManager || dataLen < 2) return;
    if (data[0] < static_cast<uint8_t>(ShootoutCmd::TOURNAMENT_END) ||
        data[0] > static_cast<uint8_t>(ShootoutCmd::ABORT))
        return;
    ShootoutCmd cmd = static_cast<ShootoutCmd>(data[0]);
    uint8_t seqId = data[1];
    const uint8_t* payload = data + 2;
    size_t payloadLen = dataLen - 2;
    switch (cmd) {
        case ShootoutCmd::TOURNAMENT_END:
            if (payloadLen >= 6)
                shootoutManager->onTournamentEndReceived(fromMac, payload, seqId);
            break;
        case ShootoutCmd::ABORT:
            shootoutManager->onAbortReceived(fromMac, seqId);
            break;
    }
}

void GameSession::onShootoutCommandAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (!shootoutManager || dataLen < 2) return;
    if (data[0] > static_cast<uint8_t>(ShootoutCmd::ABORT)) return;
    shootoutManager->onCommandAckReceived(fromMac, data[1]);
}
