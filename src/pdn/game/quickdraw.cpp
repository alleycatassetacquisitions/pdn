#include "game/quickdraw.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/drivers/logger.hpp"
#include <algorithm>
#include <array>
#include <cstring>

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
    this->shootoutManager_ = new ShootoutManager(player, wirelessManager, remoteDeviceCoordinator, chainDuelManager);
    this->shootoutManager_->setMatchManager(matchManager);
    matchManager->setShootoutManager(shootoutManager_);

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
    wirelessManager->setEspNowPacketHandler(
        PktType::kShootoutCommand,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onShootoutCommandPacket(macAddress, data, dataLen);
        },
        this
    );
    wirelessManager->setEspNowPacketHandler(
        PktType::kShootoutCommandAck,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onShootoutCommandAckPacket(macAddress, data, dataLen);
        },
        this
    );

    if (symbolWirelessManager) {
        symbolWirelessManager->initialize(wirelessManager, remoteDeviceCoordinator);
        wirelessManager->setEspNowPacketHandler(
            PktType::kSymbolMatchCommand,
            [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
                static_cast<SymbolWirelessManager*>(ctx)->processSymbolMatchCommand(macAddress, data, dataLen);
            },
            symbolWirelessManager
        );
    }

    // Clear boost/confirmed-supporters when the supporter chain drains to
    // empty while a duel is still running. Without this, a champion keeps
    // a boost from supporters that have since unplugged.
    remoteDeviceCoordinator->setChainChangeCallback([this]() {
        onChainStateChanged();
    });

    remoteDeviceCoordinator->setPeerLostCallback([this](const uint8_t* lostMac) {
        if (shootoutManager_ && shootoutManager_->active()) {
            shootoutManager_->onLocalRDCDisconnect(lostMac);
        }
    });

    // Both callbacks fire only from the HELLO link machinery, and mount replay
    // is gated on it too, so this dispatch is inert while production runs the
    // handshake path (until #159/#160).
    remoteDeviceCoordinator->setOnContextReceived(
        [this](SerialIdentifier jack, DeviceType peerType, const uint8_t* profile, size_t len) {
            onJackContextReceived(jack, peerType, profile, len);
        });
    remoteDeviceCoordinator->setOnJackChange(
        [this](SerialIdentifier jack, bool connected) {
            onJackConnectionChanged(jack, connected);
        });
}

void Quickdraw::onJackContextReceived(SerialIdentifier jack, DeviceType peerType,
                                      const uint8_t* profile, size_t len) {
    ConnectionContext context;
    context.peerType = peerType;
    // chainRole is not part of this callback's payload; the RDC records it per
    // jack before firing, so read it back from there.
    context.chainRole = remoteDeviceCoordinator->getPeerChainRole(jack);
    context.profileLen = std::min(len, context.profile.size());
    memcpy(context.profile.data(), profile, context.profileLen);
    jackContexts[static_cast<size_t>(jack)] = context;
}

void Quickdraw::onJackConnectionChanged(SerialIdentifier jack, bool connected) {
    if (!connected) {
        jackContexts[static_cast<size_t>(jack)].reset();
    }
    if (currentState == nullptr) {
        return;
    }
    JackConnectionState snapshot;
    snapshot.connected = connected;
    snapshot.context = jackContexts[static_cast<size_t>(jack)];
    currentState->onJackEvent(jack, snapshot);
}

void Quickdraw::replayConnectedJacks() {
    // getPortStatus reads handshake state while HELLO is off, so replaying
    // there would fabricate connected=true events whose context never comes;
    // jack events exist only under HELLO connectivity (#159).
    if (!remoteDeviceCoordinator->isHelloConnectivityEnabled()) {
        return;
    }
    // Only currently-connected jacks are replayed: an all-disconnected
    // snapshot carries no information for a fresh mount.
    for (size_t i = 0; i < SERIAL_JACK_COUNT; i++) {
        SerialIdentifier jack = static_cast<SerialIdentifier>(i);
        if (remoteDeviceCoordinator->getPortStatus(jack) == PortStatus::CONNECTED) {
            onJackConnectionChanged(jack, true);
        }
    }
}

void Quickdraw::onChainStateChanged() {
    if (chainDuelManager) {
        chainDuelManager->onChainStateChanged();
    }
    // Shootout disconnects flow through setPeerLostCallback, not chain-state
    // diffs — daisy chain announcements bounce in normal operation.
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

    if (chainDuelManager) {
        bool loopNow = chainDuelManager->isLoop();
        if (loopNow != lastIsLoop_) {
            LOG_W("CDM", "isLoop %d -> %d", (int)lastIsLoop_, (int)loopNow);
            lastIsLoop_ = loopNow;
        }
    }

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

    // JackConnectionState is state, not an edge: a state mounted after a jack
    // connected must not stay blind until the next connect/disconnect event,
    // so each fresh mount gets the current per-jack snapshot replayed.
    if (currentState != lastDispatchedState) {
        lastDispatchedState = currentState;
        replayConnectedJacks();
    }
}

void Quickdraw::onStateDismounted(Device* device) {
    StateMachine::onStateDismounted(device);
    // Without this, remounting the app into the same state pointer would look
    // like no change and skip the jack snapshot replay.
    lastDispatchedState = nullptr;
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

void Quickdraw::onShootoutCommandPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (!shootoutManager_ || dataLen < 2) return;
    if (data[0] > static_cast<uint8_t>(ShootoutCmd::ABORT)) return;
    ShootoutCmd cmd = static_cast<ShootoutCmd>(data[0]);
    uint8_t seqId = data[1];
    const uint8_t* payload = data + 2;
    size_t payloadLen = dataLen - 2;
    switch (cmd) {
        case ShootoutCmd::CONFIRM: {
            if (payloadLen < 6) break;
            const char* name = (payloadLen >= 6 + ShootoutManager::kNameLength)
                ? reinterpret_cast<const char*>(payload + 6) : nullptr;
            shootoutManager_->onConfirmReceived(payload, name);
            break;
        }
        case ShootoutCmd::BRACKET: {
            if (payloadLen < 1) break;
            uint8_t count = payload[0];
            if (count > ShootoutManager::kMaxBracketSize) break;
            if (payloadLen < 1 + 6 * static_cast<size_t>(count)) break;
            std::vector<std::array<uint8_t, 6>> bracket;
            bracket.reserve(count);
            for (uint8_t i = 0; i < count; i++) {
                std::array<uint8_t, 6> mac;
                memcpy(mac.data(), payload + 1 + 6 * i, 6);
                bracket.push_back(mac);
            }
            shootoutManager_->onBracketReceived(bracket, seqId);
            break;
        }
        case ShootoutCmd::MATCH_START:
            if (payloadLen >= 13)
                shootoutManager_->onMatchStartReceived(payload, payload + 6, payload[12], seqId);
            break;
        case ShootoutCmd::MATCH_RESULT:
            if (payloadLen >= 13)
                shootoutManager_->onMatchResultReceived(payload, payload + 6, payload[12], seqId, fromMac);
            break;
        case ShootoutCmd::TOURNAMENT_END:
            if (payloadLen >= 6) shootoutManager_->onTournamentEndReceived(payload, seqId);
            break;
        case ShootoutCmd::PEER_LOST:
            if (payloadLen >= 6) shootoutManager_->onPeerLostReceived(payload);
            break;
        case ShootoutCmd::ABORT:
            shootoutManager_->onAbortReceived();
            break;
    }
}

void Quickdraw::onShootoutCommandAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (!shootoutManager_ || dataLen < 2) return;
    if (data[0] > static_cast<uint8_t>(ShootoutCmd::ABORT)) return;
    ShootoutCmd cmd = static_cast<ShootoutCmd>(data[0]);
    uint8_t seqId = data[1];
    switch (cmd) {
        case ShootoutCmd::BRACKET:
            shootoutManager_->onBracketAckReceived(fromMac, seqId);
            break;
        case ShootoutCmd::MATCH_START:
            shootoutManager_->onMatchStartAckReceived(fromMac, seqId);
            break;
        case ShootoutCmd::MATCH_RESULT:
            shootoutManager_->onMatchResultAckReceived(fromMac, seqId);
            break;
        case ShootoutCmd::TOURNAMENT_END:
            shootoutManager_->onTournamentEndAckReceived(fromMac, seqId);
            break;
        default:
            break;
    }
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    // The constructor's RDC callbacks capture `this`, and the coordinator
    // outlives this app (device-owned; tests destroy Quickdraw first) — left
    // installed they would call into a destroyed object.
    remoteDeviceCoordinator->setChainChangeCallback(nullptr);
    remoteDeviceCoordinator->setPeerLostCallback(nullptr);
    remoteDeviceCoordinator->setOnContextReceived(nullptr);
    remoteDeviceCoordinator->setOnJackChange(nullptr);
    remoteDeviceCoordinator = nullptr;
    if (quickdrawWirelessManager) {
        quickdrawWirelessManager->clearCallbacks();
    }
    quickdrawWirelessManager = nullptr;
    delete matchManager;
    matchManager = nullptr;
    symbolWirelessManager = nullptr;
    delete chainDuelManager;
    chainDuelManager = nullptr;
    delete shootoutManager_;
    shootoutManager_ = nullptr;
    storageManager = nullptr;
    peerComms = nullptr;
    matches.clear();
}

void Quickdraw::populateStateMap() {

    GameContext ctx;
    ctx.player = player;
    ctx.matchManager = matchManager;
    ctx.remoteDeviceCoordinator = remoteDeviceCoordinator;
    ctx.chainDuelManager = chainDuelManager;
    ctx.shootoutManager = shootoutManager_;
    ctx.quickdrawWirelessManager = quickdrawWirelessManager;
    ctx.symbolWirelessManager = symbolWirelessManager;
    ctx.wirelessManager = wirelessManager;

    // Sub-state machines for player registration and handshake
    PlayerRegistrationApp* playerRegistration = new PlayerRegistrationApp(player, wirelessManager, matchManager, remoteDebugManager);
    // Quickdraw gameplay states
    AwakenSequence* awakenSequence = new AwakenSequence(ctx);
    Idle* idle = new Idle(ctx);

    DuelCountdown* duelCountdown = new DuelCountdown(ctx);
    Duel* duel = new Duel(ctx);
    DuelPushed* duelPushed = new DuelPushed(ctx);
    DuelReceivedResult* duelReceivedResult = new DuelReceivedResult(ctx);
    DuelResult* duelResult = new DuelResult(ctx);
    SupporterReady* supporterReady = new SupporterReady(ctx);
    this->supporterReadyState = supporterReady;

    Win* win = new Win(ctx);
    Lose* lose = new Lose(ctx);

    Sleep* sleep = new Sleep(ctx);
    UploadMatchesState* uploadMatches = new UploadMatchesState(ctx);

    // Shootout tournament states (auto-triggered by loop closure from Idle).
    auto* shProposal = new ShootoutProposal(ctx);
    auto* shBracketReveal = new ShootoutBracketReveal(ctx);
    auto* shSpectator = new ShootoutSpectator(ctx);
    auto* shEliminated = new ShootoutEliminated(ctx);
    auto* shFinalStandings = new ShootoutFinalStandings(ctx);
    auto* shAborted = new ShootoutAborted(ctx);

    SymbolState* symbol = new SymbolState(ctx);
    SymbolMatched* symbolMatched = new SymbolMatched(ctx);

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

    // Auto-trigger Shootout when the chain closes into a loop. Priority over
    // DuelCountdown/SupporterReady: in a closed ring, adjacent H-B pairs
    // otherwise look like a normal duel initiation, and the ring intent
    // (tournament) would be silently demoted to a 1v1 duel.
    {
        ChainDuelManager* cdm = chainDuelManager;
        ShootoutManager* shMgr = shootoutManager_;
        idle->addTransition(
            new StateTransition(
                [cdm, shMgr]() {
                    bool loop = cdm && cdm->isLoop();
                    bool shActive = shMgr && shMgr->active();
                    return loop && shMgr && !shActive;
                },
                shProposal));
    }

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToDuelCountdown, idle),
            duelCountdown));

    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToSupporterReady, idle),
            supporterReady));

    {
        ShootoutManager* shMgr = shootoutManager_;
        auto phaseIsAborted = [shMgr]() {
            return shMgr && shMgr->getPhase() == ShootoutManager::Phase::ABORTED;
        };
        idle->addTransition(new StateTransition(phaseIsAborted, shAborted));
        duelCountdown->addTransition(new StateTransition(phaseIsAborted, shAborted));
        duel->addTransition(new StateTransition(phaseIsAborted, shAborted));
        duelPushed->addTransition(new StateTransition(phaseIsAborted, shAborted));
        duelReceivedResult->addTransition(new StateTransition(phaseIsAborted, shAborted));
        duelResult->addTransition(new StateTransition(phaseIsAborted, shAborted));
    }

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
            [this, duelCountdown]() {
                return duelReturnsToIdle(*duelCountdown, shootoutManager_);
            },
            idle));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToShootoutSpectator, duel),
            shSpectator));

    duel->addTransition(
        new StateTransition(
            std::bind(&Duel::transitionToShootoutEliminated, duel),
            shEliminated));

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
            [this, duelPushed]() {
                return duelReturnsToIdle(*duelPushed, shootoutManager_);
            },
            idle));

    duelPushed->addTransition(
        new StateTransition(
            std::bind(&DuelPushed::transitionToDuelResult, duelPushed),
            duelResult));

    duelReceivedResult->addTransition(
        new StateTransition(
            [this, duelReceivedResult]() {
                return duelReturnsToIdle(*duelReceivedResult, shootoutManager_);
            },
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

    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToShootoutSpectator, duelResult),
            shSpectator));

    duelResult->addTransition(
        new StateTransition(
            std::bind(&DuelResult::transitionToShootoutEliminated, duelResult),
            shEliminated));

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

    // --- Shootout transitions ---
    shProposal->addTransition(
        new StateTransition(
            std::bind(&ShootoutProposal::transitionToBracketReveal, shProposal),
            shBracketReveal));
    shProposal->addTransition(
        new StateTransition(
            std::bind(&ShootoutProposal::transitionToAborted, shProposal),
            shAborted));

    shBracketReveal->addTransition(
        new StateTransition(
            std::bind(&ShootoutBracketReveal::transitionToDuelCountdown, shBracketReveal),
            duelCountdown));
    shBracketReveal->addTransition(
        new StateTransition(
            std::bind(&ShootoutBracketReveal::transitionToSpectator, shBracketReveal),
            shSpectator));
    shBracketReveal->addTransition(
        new StateTransition(
            std::bind(&ShootoutBracketReveal::transitionToAborted, shBracketReveal),
            shAborted));

    shSpectator->addTransition(
        new StateTransition(
            std::bind(&ShootoutSpectator::transitionToDuelCountdown, shSpectator),
            duelCountdown));
    shSpectator->addTransition(
        new StateTransition(
            std::bind(&ShootoutSpectator::transitionToFinalStandings, shSpectator),
            shFinalStandings));
    shSpectator->addTransition(
        new StateTransition(
            std::bind(&ShootoutSpectator::transitionToAborted, shSpectator),
            shAborted));

    shEliminated->addTransition(
        new StateTransition(
            std::bind(&ShootoutEliminated::transitionToFinalStandings, shEliminated),
            shFinalStandings));
    shEliminated->addTransition(
        new StateTransition(
            std::bind(&ShootoutEliminated::transitionToAborted, shEliminated),
            shAborted));

    shAborted->addTransition(
        new StateTransition(
            std::bind(&ShootoutAborted::transitionToIdle, shAborted),
            idle));

    // Cable-event reset after TOURNAMENT_END: when the physical ring opens,
    // route through Sleep so the cooldown period elapses before the next
    // proposal can fire. Going straight to Idle let stale RDC chain state
    // (still advertising the old ring) feed back into CDM::isLoop on the
    // same loop tick as unplug, triggering a phantom Proposal with no peers
    // left to confirm.
    shFinalStandings->addTransition(
        new StateTransition(
            std::bind(&ShootoutFinalStandings::transitionToSleep, shFinalStandings),
            sleep));

    // --- Symbol-match transitions ---
    idle->addTransition(
        new StateTransition(
            std::bind(&Idle::transitionToSymbol, idle),
            symbol));

    symbol->addTransition(
        new StateTransition(
            std::bind(&SymbolState::transitionToIdle, symbol),
            idle));

    symbol->addTransition(
        new StateTransition(
            std::bind(&SymbolState::transitionToSymbolMatched, symbol),
            symbolMatched));

    symbolMatched->addTransition(
        new StateTransition(
            std::bind(&SymbolMatched::transitionToSymbol, symbolMatched),
            symbol));

    symbolMatched->addTransition(
        new StateTransition(
            std::bind(&SymbolMatched::transitionToIdle, symbolMatched),
            idle));

    // State map - order matters: first entry is the initial state
    stateMap.push_back(playerRegistration);
    stateMap.push_back(awakenSequence);
    stateMap.push_back(idle);
    stateMap.push_back(supporterReady);
    stateMap.push_back(duelCountdown);
    stateMap.push_back(duel);
    stateMap.push_back(duelPushed);
    stateMap.push_back(duelReceivedResult);
    stateMap.push_back(duelResult);
    stateMap.push_back(win);
    stateMap.push_back(lose);
    stateMap.push_back(uploadMatches);
    stateMap.push_back(sleep);
    stateMap.push_back(shProposal);
    stateMap.push_back(shBracketReveal);
    stateMap.push_back(shSpectator);
    stateMap.push_back(shEliminated);
    stateMap.push_back(shFinalStandings);
    stateMap.push_back(shAborted);
    stateMap.push_back(symbol);
    stateMap.push_back(symbolMatched);
}
