#include "game/quickdraw-states.hpp"
#include "id-generator.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"


BountySendConnectionConfirmedState::BountySendConnectionConfirmedState(Player *player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager) : BaseHandshakeState(BOUNTY_SEND_CC_STATE) {
    this->matchManager = matchManager;
    this->player = player;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
}

BountySendConnectionConfirmedState::~BountySendConnectionConfirmedState() {
    player = nullptr;
    matchManager = nullptr;
    quickdrawWirelessManager = nullptr;
}



void BountySendConnectionConfirmedState::onStateMounted(Device *PDN) {
    if (!player) {
        LOG_E("BOUNTY_SEND_CC", "Player is null in onStateMounted");
        return;
    }

    if (player->getOpponentMacAddress().empty()) {
        LOG_E("BOUNTY_SEND_CC", "Opponent MAC address is empty");
        return;
    }

    LOG_I("BOUNTY_SEND_CC", "State mounted");
    
    std::string bountyId = player->getUserID();
    if (bountyId.empty()) {
        LOG_E("BOUNTY_SEND_CC", "Player ID is empty");
        return;
    }

    std::string matchId = IdGenerator(SimpleTimer::getPlatformClock()->milliseconds()).generateId();
    if (matchId.empty()) {
        LOG_E("BOUNTY_SEND_CC", "Failed to generate match ID");
        return;
    }
    
    LOG_I("BOUNTY_SEND_CC", "Broadcasting packet with matchId: %s, bountyId: %s", 
             matchId.c_str(), bountyId.c_str());
    
    quickdrawWirelessManager->setPacketReceivedCallback(
        std::bind(&BountySendConnectionConfirmedState::onQuickdrawCommandReceived, this, std::placeholders::_1)
    );

    try {
        Match initialMatch(matchId, "", bountyId);  // Empty hunter ID initially
        quickdrawWirelessManager->broadcastPacket(
            player->getOpponentMacAddress(),
            CONNECTION_CONFIRMED,
            initialMatch
        );
        LOG_I("BOUNTY_SEND_CC", "Sent CONNECTION_CONFIRMED");
    } catch (const std::exception& e) {
        LOG_E("BOUNTY_SEND_CC", "Failed to send CONNECTION_CONFIRMED: %s", e.what());
    }
}

void BountySendConnectionConfirmedState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (!player) {
        LOG_E("BOUNTY_SEND_CC", "Player is null in command handler");
        return;
    }

    if (player->getOpponentMacAddress().empty()) {
        LOG_E("BOUNTY_SEND_CC", "Opponent MAC address is empty");
        return;
    }

    LOG_I("BOUNTY_SEND_CC", "Received command: %d", command.command);

    if (command.command == HUNTER_RECEIVE_MATCH) {
        LOG_I("BOUNTY_SEND_CC", "Received HUNTER_RECEIVE_MATCH command");

        // Validate received match data
        if (command.match.getMatchId().empty()) {
            LOG_E("BOUNTY_SEND_CC", "Received empty match ID");
            return;
        }
        if (command.match.getHunterId().empty()) {
            LOG_E("BOUNTY_SEND_CC", "Received empty hunter ID");
            return;
        }
        if (command.match.getBountyId().empty()) {
            LOG_E("BOUNTY_SEND_CC", "Received empty bounty ID");
            return;
        }

        LOG_I("BOUNTY_SEND_CC", "Received match - ID: %s, Hunter: %s, Bounty: %s",
                 command.match.getMatchId().c_str(),
                 command.match.getHunterId().c_str(),
                 command.match.getBountyId().c_str());

        Match* match = matchManager->receiveMatch(command.match);
        if (!match) {
            LOG_E("BOUNTY_SEND_CC", "Failed to receive match");
            return;
        }

        try {
            quickdrawWirelessManager->broadcastPacket(
                player->getOpponentMacAddress(),
                BOUNTY_FINAL_ACK,
                *match
            );
            LOG_I("BOUNTY_SEND_CC", "Sent BOUNTY_FINAL_ACK");

            // Start waiting for HUNTER_READY
            waitingForHunterReady = true;
            retryCount = 0;
            retryTimer.setTimer(retryTimeout);
            LOG_I("BOUNTY_SEND_CC", "Waiting for HUNTER_READY (timeout: %dms)", retryTimeout);
        } catch (const std::exception& e) {
            LOG_E("BOUNTY_SEND_CC", "Failed to send BOUNTY_FINAL_ACK: %s", e.what());
        }
    } else if (command.command == HUNTER_READY) {
        LOG_I("BOUNTY_SEND_CC", "Received HUNTER_READY from opponent");

        if (waitingForHunterReady) {
            waitingForHunterReady = false;
            transitionToConnectionSuccessfulState = true;
            LOG_I("BOUNTY_SEND_CC", "Handshake complete - both devices confirmed");
        } else {
            LOG_W("BOUNTY_SEND_CC", "Received HUNTER_READY but not waiting for it");
        }
    } else {
        LOG_W("BOUNTY_SEND_CC", "Received unexpected command: %d", command.command);
    }
}

void BountySendConnectionConfirmedState::onStateLoop(Device *PDN) {
    if (!waitingForHunterReady) {
        return;
    }

    retryTimer.updateTime();
    if (retryTimer.expired()) {
        if (retryCount < maxRetries) {
            // Retry sending BOUNTY_FINAL_ACK
            retryCount++;
            LOG_W("BOUNTY_SEND_CC", "HUNTER_READY timeout - retry %d/%d", retryCount, maxRetries);

            Match* match = matchManager->getCurrentMatch();
            if (!match) {
                LOG_E("BOUNTY_SEND_CC", "No current match for retry");
                return;
            }

            try {
                quickdrawWirelessManager->broadcastPacket(
                    player->getOpponentMacAddress(),
                    BOUNTY_FINAL_ACK,
                    *match
                );
                LOG_I("BOUNTY_SEND_CC", "Re-sent BOUNTY_FINAL_ACK");
                retryTimer.setTimer(retryTimeout);
            } catch (const std::exception& e) {
                LOG_E("BOUNTY_SEND_CC", "Failed to retry BOUNTY_FINAL_ACK: %s", e.what());
            }
        } else {
            LOG_E("BOUNTY_SEND_CC", "Max retries exhausted, falling back to global timeout");
            waitingForHunterReady = false;
            // Don't set transition flag - let the global timeout handle it
        }
    }
}

void BountySendConnectionConfirmedState::onStateDismounted(Device *PDN) {
    transitionToConnectionSuccessfulState = false;
    waitingForHunterReady = false;
    retryCount = 0;
    retryTimer.invalidate();
    LOG_I("BOUNTY_SEND_CC", "State dismounted");
    BaseHandshakeState::resetTimeout();
    quickdrawWirelessManager->clearCallbacks();
}

bool BountySendConnectionConfirmedState::transitionToConnectionSuccessful() {
    return transitionToConnectionSuccessfulState;
}
