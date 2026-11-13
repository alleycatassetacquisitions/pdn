#include "game/game-session.hpp"
#include "device/drivers/logger.hpp"

GameSession::GameSession(Player* player, Device* pdn, SymbolWirelessManager* symbolWirelessManager)
    : pdn(pdn)
    , wirelessManager(pdn->getWirelessManager())
    , context(makeContext(player, pdn, symbolWirelessManager))
    , recordRelay(new RecordRelay(wirelessManager, context.remoteDeviceCoordinator,
                                  context.matchManager, context.shootoutManager)) {
    context.matchManager->setBoostProvider([this]() -> unsigned long {
        return context.posseManager ? context.posseManager->getBoostMs() : 0;
    });

    // The Player is the authority on this device's identity; RDC only carries it.
    context.remoteDeviceCoordinator->setSelfProfileProvider(
    [this]() -> PlayerProfile { return context.player->toProfile(); });

    // Registration flips hunter/bounty long after the jacks came up, and the
    // context exchange only runs on connect, so the flip has to push itself.
    player->setOnRoleChanged([this]() {
        context.remoteDeviceCoordinator->resendContext();
        context.posseManager->onChainStateChanged();
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
    if (context.player) context.player->setOnRoleChanged(nullptr);
    if (context.remoteDeviceCoordinator) {
        context.remoteDeviceCoordinator->setSelfProfileProvider(nullptr);
    }
    context.player = nullptr;
    // setBoostProvider is the one install with no clear: matchManager owns that slot
    // and is deleted below.
    context.remoteDeviceCoordinator = nullptr;
    context.symbolWirelessManager = nullptr;
    // First of the owned objects: it reads the match and shootout managers and
    // observes both.
    delete recordRelay;
    recordRelay = nullptr;
    // Managers before matchManager: shootoutManager holds a raw MatchManager*
    // and dereferences it when priming a bracket match.
    delete context.posseManager;
    context.posseManager = nullptr;
    delete context.shootoutManager;
    context.shootoutManager = nullptr;
    delete context.matchManager;
    context.matchManager = nullptr;
}

void GameSession::sync() {
    if (context.posseManager) {
        context.posseManager->sync();

        bool inRingNow = context.remoteDeviceCoordinator->isInRing();
        if (inRingNow != lastIsInRing) {
            LOG_W("RDC", "isInRing %d -> %d", (int)lastIsInRing, (int)inRingNow);
            lastIsInRing = inRingNow;
        }
    }
    if (context.shootoutManager) context.shootoutManager->sync();

    logRetryStats();
}

void GameSession::setCrashRecordSource(RecordSource<CrashRecordTraits>* crashRecordSource) {
    recordRelay->setCrashRecordSource(crashRecordSource);
}

GameContext GameSession::makeContext(Player* player, Device* pdn,
                                     SymbolWirelessManager* symbolWirelessManager) {
    WirelessManager* wirelessManager = pdn->getWirelessManager();
    GameContext context;
    context.player = player;
    context.remoteDeviceCoordinator = pdn->getRemoteDeviceCoordinator();
    context.symbolWirelessManager = symbolWirelessManager;
    context.matchManager =
    new MatchManager(player, pdn->getStorage(), wirelessManager, context.remoteDeviceCoordinator);
    context.posseManager =
    new PosseManager(player, wirelessManager, context.remoteDeviceCoordinator);
    context.shootoutManager = new ShootoutManager(
    player, wirelessManager, context.remoteDeviceCoordinator, context.matchManager);
    return context;
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
    if (context.posseManager != nullptr) {
        PosseManager::AckLatency latency = context.posseManager->getAckLatency();
        ackCount = latency.count;
        ackMean = latency.count ? (latency.sumMs / latency.count) : 0;
    }
    LOG_W("STATS", "RELIABLE s=%u r=%u ab=%u posseAck=%u/%lums", (unsigned)t.sends,
          (unsigned)t.retries, (unsigned)t.abandons, ackCount, ackMean);
    statsLogTimer.setTimer(STATS_LOG_INTERVAL_MS);
}
