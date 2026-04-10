#include "../../include/game/quickdraw.hpp"
#include "wireless/peer-comms-types.hpp"
#include "device/drivers/logger.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager, SymbolWirelessManager* symbolWirelessManager): StateMachine(QUICKDRAW_APP_ID) {
    this->player = player;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    this->symbolWirelessManager = symbolWirelessManager;
    this->remoteDebugManager = remoteDebugManager;
    this->wirelessManager = PDN->getWirelessManager();
    this->matchManager = new MatchManager();
    this->storageManager = PDN->getStorage();
    this->peerComms = PDN->getPeerComms();
    this->remoteDeviceCoordinator = PDN->getRemoteDeviceCoordinator();

    this->chainDuelManager = new ChainDuelManager(player, wirelessManager, remoteDeviceCoordinator);

    matchManager->initialize(player, storageManager, quickdrawWirelessManager);
    matchManager->setBoostProvider([this]() -> unsigned long {
        return chainDuelManager ? chainDuelManager->getBoostMs() : 0;
    });
    matchManager->setRemoteDeviceCoordinator(remoteDeviceCoordinator);

    quickdrawWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, matchManager, std::placeholders::_1)
    );

    // Chain game event + confirm wireless packet handlers.
    wirelessManager->setEspNowPacketHandler(
        PktType::kChainGameEvent,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onChainGameEventPacket(macAddress, data, dataLen);
        },
        this
    );
    wirelessManager->setEspNowPacketHandler(
        PktType::kChainGameEventAck,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onChainGameEventAckPacket(macAddress, data, dataLen);
        },
        this
    );
    wirelessManager->setEspNowPacketHandler(
        PktType::kChainConfirm,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onChainConfirmPacket(macAddress, data, dataLen);
        },
        this
    );
    wirelessManager->setEspNowPacketHandler(
        PktType::kRoleAnnounce,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onRoleAnnouncePacket(macAddress, data, dataLen);
        },
        this
    );
    wirelessManager->setEspNowPacketHandler(
        PktType::kRoleAnnounceAck,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onRoleAnnounceAckPacket(macAddress, data, dataLen);
        },
        this
    );

    // Clear boost/confirmed-supporters when the supporter chain drains to
    // empty while a duel is still running. Without this, a champion keeps
    // a boost from supporters that have since unplugged.
    remoteDeviceCoordinator->setChainChangeCallback([this]() {
        onChainStateChanged();
    });
}

void Quickdraw::onChainStateChanged() {
    if (chainDuelManager) {
        chainDuelManager->onChainStateChanged();
    }
}

void Quickdraw::onRoleAnnouncePacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(RoleAnnouncePayload) || !chainDuelManager) return;
    const RoleAnnouncePayload* payload = reinterpret_cast<const RoleAnnouncePayload*>(data);
    chainDuelManager->onRoleAnnounceReceived(
        fromMac, payload->role, payload->championMac, payload->seqId);
}

void Quickdraw::onRoleAnnounceAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(RoleAnnounceAckPayload) || !chainDuelManager) return;
    const RoleAnnounceAckPayload* payload = reinterpret_cast<const RoleAnnounceAckPayload*>(data);
    chainDuelManager->onRoleAnnounceAckReceived(fromMac, payload->seqId);
}

void Quickdraw::onStateLoop(Device *PDN) {
    if (chainDuelManager) chainDuelManager->sync();

    if (!statsLogTimer_.isRunning()) {
        statsLogTimer_.setTimer(kStatsLogIntervalMs);
    } else if (statsLogTimer_.expired()) {
        if (remoteDeviceCoordinator != nullptr && chainDuelManager != nullptr) {
            auto r = remoteDeviceCoordinator->getRetryStats();
            auto c = chainDuelManager->getRetryStats();
            unsigned long rMean = r.ackCount ? (r.ackLatencyMsSum / r.ackCount) : 0;
            unsigned long cMean = c.ackCount ? (c.ackLatencyMsSum / c.ackCount) : 0;
            // LOG_W (not LOG_I) because firmware builds with CORE_DEBUG_LEVEL=2
            // which strips info-level calls.
            LOG_W("STATS",
                "RDC s=%u r=%u ab=%u ack=%u/%lums | CDM s=%u r=%u ab=%u ack=%u/%lums",
                (unsigned)r.sends, (unsigned)r.retries, (unsigned)r.abandons,
                (unsigned)r.ackCount, rMean,
                (unsigned)c.sends, (unsigned)c.retries, (unsigned)c.abandons,
                (unsigned)c.ackCount, cMean);
        }
        statsLogTimer_.setTimer(kStatsLogIntervalMs);
    }

    StateMachine::onStateLoop(PDN);
}

void Quickdraw::onChainGameEventPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(ChainGameEventPayload)) return;
    if (!chainDuelManager || !chainDuelManager->isKnownGameEventSender(fromMac)) return;

    const ChainGameEventPayload* payload = reinterpret_cast<const ChainGameEventPayload*>(data);

    // Out-of-state events dropped silently; champion's retry machine bounds traffic cost.
    if (supporterReadyState != nullptr && currentState != nullptr
        && currentState->getStateId() == SUPPORTER_READY) {
        supporterReadyState->onChainGameEventReceived(payload->event_type, fromMac);
    }

    // ACK regardless of whether we were in SupporterReady. Not ACKing after
    // leaving the state would let the champion keep retrying a WIN/LOSS we
    // already received and can't act on. seqId=0 is the sentinel for
    // fire-and-forget events (COUNTDOWN/DRAW) and must not be ACKed.
    if (payload->seqId != 0) {
        chainDuelManager->sendGameEventAck(fromMac, payload->seqId);
    }
}

void Quickdraw::onChainGameEventAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(ChainGameEventAckPayload) || !chainDuelManager) return;
    const ChainGameEventAckPayload* payload = reinterpret_cast<const ChainGameEventAckPayload*>(data);
    chainDuelManager->onChainGameEventAckReceived(fromMac, payload->seqId);
}

void Quickdraw::onChainConfirmPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen != sizeof(ChainConfirmPayload)) return;
    if (!chainDuelManager) return;
    const ChainConfirmPayload* payload = reinterpret_cast<const ChainConfirmPayload*>(data);
    chainDuelManager->onConfirmReceived(fromMac, payload->originatorMac, payload->seqId);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    remoteDeviceCoordinator = nullptr;
    quickdrawWirelessManager->clearCallbacks();
    quickdrawWirelessManager = nullptr;
    delete matchManager;
    symbolWirelessManager = nullptr;
    matchManager = nullptr;
    delete chainDuelManager;
    chainDuelManager = nullptr;
    storageManager = nullptr;
    peerComms = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {

    // Sub-state machines for player registration and handshake
    PlayerRegistrationApp* playerRegistration = new PlayerRegistrationApp(player, wirelessManager, matchManager, remoteDebugManager);
    // Quickdraw gameplay states
    AwakenSequence* awakenSequence = new AwakenSequence(player);
    Idle* idle = new Idle(player, matchManager, remoteDeviceCoordinator, chainDuelManager);

    DuelCountdown* duelCountdown = new DuelCountdown(player, matchManager, remoteDeviceCoordinator, chainDuelManager);
    Duel* duel = new Duel(player, matchManager, remoteDeviceCoordinator, chainDuelManager);
    DuelPushed* duelPushed = new DuelPushed(player, matchManager, remoteDeviceCoordinator);
    DuelReceivedResult* duelReceivedResult = new DuelReceivedResult(player, matchManager, remoteDeviceCoordinator);
    DuelResult* duelResult = new DuelResult(player, matchManager, quickdrawWirelessManager);
    SupporterReady* supporterReady = new SupporterReady(player, remoteDeviceCoordinator, chainDuelManager);
    this->supporterReadyState = supporterReady;
    
    Win* win = new Win(player, chainDuelManager, matchManager);
    Lose* lose = new Lose(player, chainDuelManager, matchManager);
    
    Sleep* sleep = new Sleep(player);
    UploadMatchesState* uploadMatches = new UploadMatchesState(player, wirelessManager, matchManager);

    SymbolState* symbol = new SymbolState(player, matchManager, remoteDeviceCoordinator, symbolWirelessManager);

    // --- Transitions from PlayerRegistration app ---
    playerRegistration->addTransition(
        new StateTransition(
            std::bind(&PlayerRegistrationApp::readyForGameplay, playerRegistration),
            awakenSequence));

    // --- Quickdraw gameplay transitions ---
    awakenSequence->addTransition(
        new StateTransition(
            std::bind(&AwakenSequence::transitionToIdle, awakenSequence),
            idle));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToDuelCountdown, idle),
            duelCountdown));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToSupporterReady, idle),
            supporterReady));

    supporterReady->addTransition(
        new StateTransition(
            std::bind(&SupporterReady::transitionToIdle, supporterReady),
            idle));

    // --- Duel flow ---
    duelCountdown->addTransition(
        new StateTransition(
            std::bind(&DuelCountdown::shallWeBattle, duelCountdown),
            duel));

    duelCountdown->addTransition(
        new StateTransition(
            std::bind(&DuelCountdown::disconnectedBackToIdle, duelCountdown),
            idle));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToIdle, duel),
            idle));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToDuelReceivedResult, duel),
            duelReceivedResult));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToDuelPushed, duel),
            duelPushed));

    duelPushed->addTransition(
        new StateTransition(
            std::bind(&DuelPushed::disconnectedBackToIdle, duelPushed),
            idle));

    duelPushed->addTransition(
        new StateTransition(
            std::bind(&DuelPushed::transitionToDuelResult, duelPushed),
            duelResult));

    duelReceivedResult->addTransition(
        new StateTransition(
            std::bind(&DuelReceivedResult::disconnectedBackToIdle, duelReceivedResult),
            idle));

    duelReceivedResult->addTransition(
        new StateTransition(
            std::bind(&DuelReceivedResult::transitionToDuelResult, duelReceivedResult),
            duelResult));

    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToWin, duelResult),
            win));

    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToLose, duelResult),
            lose));

    // --- Post-game flow ---
    win->addTransition(
        new StateTransition(
            std::bind(&Win::resetGame, win),
            uploadMatches));

    lose->addTransition(
        new StateTransition(
            std::bind(&Lose::resetGame, lose),
            uploadMatches));

    uploadMatches->addTransition(
        new StateTransition(
            std::bind(&UploadMatchesState::transitionToSleep, uploadMatches),
            sleep));

    sleep->addTransition(
        new StateTransition(
            std::bind(&Sleep::transitionToAwakenSequence, sleep),
            awakenSequence));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToSymbol, idle),
            symbol));
    
    symbol->addTransition(
        new StateTransition(
            std::bind(&SymbolState::transitionToIdle, symbol),
            idle));

    // State map - order matters: first entry is the initial state
    stateMap.push_back(playerRegistration);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(duelPushed);
    stateMap.push_back(duelReceivedResult);
    stateMap.push_back(duelResult);
    stateMap.push_back(win);
    stateMap.push_back(lose);
    stateMap.push_back(uploadMatches);
    stateMap.push_back(sleep);
    stateMap.push_back(symbol);
}
