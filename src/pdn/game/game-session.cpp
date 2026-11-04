#include "game/game-session.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/logger.hpp"

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

    posseManager->setOnGameEventReceived([this](uint8_t eventType, const uint8_t* fromMac) {
        SupporterReady* supporterReady = getMountedSupporterReady();
        if (supporterReady != nullptr) {
            supporterReady->onChainGameEventReceived(eventType, fromMac);
        }
    });

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
    // Every slot below holds `this` on an object that outlives this session,
    // so each must be dropped before that pointer dangles.
    if (pdn) pdn->setTickCallback(nullptr);
    pdn = nullptr;
    if (player) player->setOnRoleChanged(nullptr);
    if (remoteDeviceCoordinator) remoteDeviceCoordinator->setSelfProfileProvider(nullptr);
    player = nullptr;
    // setBoostProvider and setOnGameEventReceived need no clear: matchManager
    // and posseManager own those slots and are deleted below.
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
    if (matchManager) matchManager->sync();

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
    if (symbolWirelessManager) symbolWirelessManager->sync();

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
    // line to judge radio health would otherwise be shown the posse's
    // retries and told nothing about the tournament's.
    if (posseManager != nullptr) {
        PosseManager::RetryStats c = posseManager->getRetryStats();
        unsigned long cMean = c.ackCount ? (c.ackLatencyMsSum / c.ackCount) : 0;
        LOG_W("STATS", "POSSE s=%u r=%u ab=%u ack=%u/%lums", (unsigned)c.sends, (unsigned)c.retries,
              (unsigned)c.abandons, (unsigned)c.ackCount, cMean);
    }
    if (shootoutManager != nullptr) {
        const Resender::Stats& s = shootoutManager->getRetryStats();
        LOG_W("STATS", "SHT s=%u r=%u ab=%u",
              (unsigned)s.sends, (unsigned)s.retries, (unsigned)s.abandons);
    }
    statsLogTimer.setTimer(STATS_LOG_INTERVAL_MS);
}
