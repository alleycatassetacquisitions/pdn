#include "../../include/game/quickdraw.hpp"
#include "game/jack-roles.hpp"

Quickdraw::Quickdraw(Player* player, Device* PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager): StateMachine(QUICKDRAW_APP_ID) {
    this->player = player;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    this->remoteDebugManager = remoteDebugManager;
    this->wirelessManager = PDN->getWirelessManager();
    this->matchManager = new MatchManager();
    this->storageManager = PDN->getStorage();
    this->peerComms = PDN->getPeerComms();
    this->remoteDeviceCoordinator = PDN->getRemoteDeviceCoordinator();

    matchManager->initialize(player, storageManager, quickdrawWirelessManager);

    quickdrawWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchEvents, matchManager, std::placeholders::_1)
    );

    PDN->getPeerComms()->setPacketHandler(PktType::kTeamCommand,
        [](const uint8_t* src, const uint8_t* data, size_t len, void* ctx) {
            static_cast<Quickdraw*>(ctx)->onTeamPacketReceived(src, data, len);
        }, this);
}

Quickdraw::~Quickdraw() {
    player = nullptr;
    remoteDeviceCoordinator = nullptr;
    quickdrawWirelessManager->clearCallbacks();
    quickdrawWirelessManager = nullptr;
    matchManager = nullptr;
    storageManager = nullptr;
    peerComms = nullptr;
}

void Quickdraw::onTeamPacketReceived(const uint8_t* src, const uint8_t* data, size_t len) {
    if (len < 1) return;
    auto cmd = static_cast<TeamCommandType>(data[0]);

    // REGISTER_INVITE uses a larger packet with champion MAC
    if (cmd == TeamCommandType::REGISTER_INVITE) {
        if (len == sizeof(RegisterInvitePacket)) {
            auto* invite = reinterpret_cast<const RegisterInvitePacket*>(data);
            // Reject invite to support ourselves — ring detected
            const uint8_t* myMac = peerComms->getMacAddress();
            if (myMac && memcmp(invite->championMac, myMac, 6) == 0) return;
            // Reject invite from different-role champion
            bool champIsHunter = invite->championIsHunter != 0;
            if (champIsHunter != player->isHunter()) {
                sendTeamPacket(peerComms, src, TeamCommandType::INVITE_REJECT, 0);
                return;
            }
            memcpy(inviteChampionMac_, invite->championMac, 6);
            memcpy(inviteSenderMac_, src, 6);
            strncpy(inviteChampionName_, invite->championName, CHAMPION_NAME_MAX - 1);
            inviteChampionName_[CHAMPION_NAME_MAX - 1] = '\0';
            invitePosition_ = invite->position < 255 ? invite->position + 1 : 255;
            inviteChampionIsHunter_ = invite->championIsHunter != 0;
            hasTeamInvite_ = true;
        }
        return;
    }

    if (cmd == TeamCommandType::INVITE_REJECT) {
        inviteRejected_ = true;
        return;
    }

    if (len != sizeof(TeamPacket)) return;
    auto* pkt = reinterpret_cast<const TeamPacket*>(data);
    int position = pkt->eventType;

    switch (cmd) {
        case TeamCommandType::REGISTER:
            if (findSupporterIndex(src) < 0 && supporterMacCount_ < MAX_SUPPORTERS) {
                memcpy(supporterMacs_[supporterMacCount_++], src, 6);
            }
            break;
        case TeamCommandType::DEREGISTER: {
            int idx = findSupporterIndex(src);
            if (idx >= 0) {
                memmove(&supporterMacs_[idx], &supporterMacs_[idx+1], (supporterMacCount_ - idx - 1) * 6);
                supporterMacCount_--;
                if (confirmedSupporters_ > supporterMacCount_) confirmedSupporters_ = supporterMacCount_;
                matchManager->setBoost(confirmedSupporters_ * 15);
            }
            break;
        }
        case TeamCommandType::CONFIRM:
            if (findSupporterIndex(src) < 0) break;
            if (confirmedSupporters_ >= supporterMacCount_) break;
            confirmedSupporters_++;
            matchManager->setBoost(confirmedSupporters_ * 15);
            break;
        case TeamCommandType::GAME_EVENT:
            if (position > static_cast<int>(GameEventType::LOSS)) break;
            if (memcmp(src, inviteChampionMac_, 6) != 0) break;
            if (gameEventCallback_) gameEventCallback_(static_cast<GameEventType>(position));
            break;
    }
}

// Detect state transitions to broadcast game events to supporters
void Quickdraw::onStateLoop(Device* PDN) {
    StateMachine::onStateLoop(PDN);
    int sid = getCurrentState() ? getCurrentState()->getStateId() : -1;
    if (sid != lastStateId_) {
        LOG_W("QD", "State %d -> %d", lastStateId_, sid);
        onStateChanged(sid);
        lastStateId_ = sid;
    }
}

void Quickdraw::onStateChanged(int newStateId) {
    if (supporterMacCount_ > 0) {
        switch (newStateId) {
            case DUEL_COUNTDOWN: broadcastTeamEvent(peerComms, GameEventType::COUNTDOWN); break;
            case DUEL:           broadcastTeamEvent(peerComms, GameEventType::DRAW); break;
            case WIN:            broadcastTeamEvent(peerComms, GameEventType::WIN); break;
            case LOSE:           broadcastTeamEvent(peerComms, GameEventType::LOSS); break;
            default: break;
        }
    }

    if (newStateId == IDLE) {
        LOG_W("QD", "-> IDLE, clearing chain+match");
        clearChainState();
        matchManager->clearCurrentMatch("onIdle");
    }
}

void Quickdraw::clearChainState() {
    confirmedSupporters_ = 0;
    supporterMacCount_ = 0;
    memset(supporterMacs_, 0, sizeof(supporterMacs_));
    hasTeamInvite_ = false;
    invitePosition_ = 0;
    memset(inviteChampionMac_, 0, 6);
    memset(inviteSenderMac_, 0, 6);
    inviteChampionName_[0] = '\0';
    inviteChampionIsHunter_ = false;
    inviteRejected_ = false;
}

int Quickdraw::findSupporterIndex(const uint8_t* mac) const {
    for (int i = 0; i < supporterMacCount_; i++) {
        if (memcmp(supporterMacs_[i], mac, 6) == 0) return i;
    }
    return -1;
}

bool Quickdraw::shouldTransitionToSupporter(SupporterReady* supporterReady) {
    if (!hasTeamInvite_) return false;
    auto reject = [&]() {
        const uint8_t* senderMac = remoteDeviceCoordinator->getPeerMac(opponentJack(player));
        if (senderMac) {
            sendTeamPacket(peerComms, senderMac, TeamCommandType::INVITE_REJECT, 0);
        }
        hasTeamInvite_ = false;
    };

    // Don't become supporter if a match is ready to duel
    if (matchManager->isMatchReady()) {
        reject();
        return false;
    }

    // Invite must come from the device on our opponent jack (physical chain neighbor)
    SerialIdentifier oJack = opponentJack(player);
    const uint8_t* oJackMac = remoteDeviceCoordinator->getPeerMac(oJack);
    if (!oJackMac || memcmp(inviteSenderMac_, oJackMac, 6) != 0) {
        reject();
        return false;
    }

    // Champion must be same role as us
    if (inviteChampionIsHunter_ != player->isHunter()) {
        reject();
        return false;
    }

    // Reject if champion is on our supporter jack — that's a loop
    SerialIdentifier sJack = supporterJack(player);
    const uint8_t* sPeerMac = remoteDeviceCoordinator->getPeerMac(sJack);
    if (sPeerMac && memcmp(inviteChampionMac_, sPeerMac, 6) == 0) {
        reject();
        return false;
    }

    supporterReady->setChampionMac(inviteChampionMac_);
    supporterReady->setChampionName(inviteChampionName_);
    supporterReady->setPosition(invitePosition_);
    supporterReady->setChampionIsHunter(inviteChampionIsHunter_);
    hasTeamInvite_ = false;
    return true;
}

void Quickdraw::populateStateMap() {

    // Sub-state machines for player registration and handshake
    PlayerRegistrationApp* playerRegistration = new PlayerRegistrationApp(player, wirelessManager, matchManager, remoteDebugManager);
    // Quickdraw gameplay states
    AwakenSequence* awakenSequence = new AwakenSequence(player);
    Idle* idle = new Idle(player, matchManager, remoteDeviceCoordinator,
        [this]() { return getSupporterCount(); },
        [this]() { clearChainState(); },
        [this]() { return isInviteRejected(); });

    SupporterReady* supporterReady = new SupporterReady(player, remoteDeviceCoordinator, this);

    DuelCountdown* duelCountdown = new DuelCountdown(player, matchManager, remoteDeviceCoordinator);
    Duel* duel = new Duel(player, matchManager, remoteDeviceCoordinator);
    DuelPushed* duelPushed = new DuelPushed(player, matchManager, remoteDeviceCoordinator);
    DuelReceivedResult* duelReceivedResult = new DuelReceivedResult(player, matchManager, remoteDeviceCoordinator);
    DuelResult* duelResult = new DuelResult(player, matchManager, quickdrawWirelessManager);
    
    Win* win = new Win(player);
    Lose* lose = new Lose(player);
    
    Sleep* sleep = new Sleep(player);
    UploadMatchesState* uploadMatches = new UploadMatchesState(player, wirelessManager, matchManager);

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
    idle->addTransition(new StateTransition(
        std::bind(&Quickdraw::shouldTransitionToSupporter, this, supporterReady),
        supporterReady));

    // --- Supporter ready transitions ---
    supporterReady->addTransition(
        new StateTransition(
            std::bind(&SupporterReady::transitionToWin, supporterReady),
            win));
    supporterReady->addTransition(
        new StateTransition(
            std::bind(&SupporterReady::transitionToLose, supporterReady),
            lose));
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
    stateMap.push_back(supporterReady);
}
