#include "game/game-session.hpp"
#include "device/drivers/logger.hpp"

// The initializer list is in declaration order, which is also dependency order:
// the managers read the device-owned ones pulled off the PDN above them.
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
    // Every slot below holds `this` by lambda capture on an object that outlives
    // this session, so each must be dropped before that pointer dangles.
    if (pdn) pdn->setTickCallback(nullptr);
    pdn = nullptr;
    if (player) player->setOnRoleChanged(nullptr);
    if (remoteDeviceCoordinator) remoteDeviceCoordinator->setSelfProfileProvider(nullptr);
    player = nullptr;
    // setBoostProvider is the one install with no clear: matchManager owns that slot
    // and is deleted below.
    remoteDeviceCoordinator = nullptr;
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
    // strips info-level calls.
    const Resender::Stats& t = wirelessManager->getReliableTransport()->getStats();
    unsigned long ackMean = 0;
    unsigned ackCount = 0;
    if (posseManager != nullptr) {
        PosseManager::AckLatency latency = posseManager->getAckLatency();
        ackCount = latency.count;
        ackMean = latency.count ? (latency.sumMs / latency.count) : 0;
    }
    LOG_W("STATS", "RELIABLE s=%u r=%u ab=%u posseAck=%u/%lums", (unsigned)t.sends,
          (unsigned)t.retries, (unsigned)t.abandons, ackCount, ackMean);
    statsLogTimer.setTimer(STATS_LOG_INTERVAL_MS);
}
