#include "id-generator.hpp"
#include "game/quickdraw-states.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
// Opening Handshake State for a Hunter. we have sent our mac address over serial and are waiting
// for the opponent to send the match id and their user id.

HunterSendIdState::HunterSendIdState(Player *player) : BaseHandshakeState(HUNTER_SEND_ID_STATE) {
    this->player = player;
}

HunterSendIdState::~HunterSendIdState() {
    player = nullptr;
}


void HunterSendIdState::onStateMounted(Device *PDN) {
    LOG_I("HUNTER_SEND_ID", "State mounted");
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&HunterSendIdState::onQuickdrawCommandReceived, this, std::placeholders::_1));
}

void HunterSendIdState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (!player) {
        LOG_E("HUNTER_SEND_ID", "Player is null in command handler");
        return;
    }

    LOG_I("HUNTER_SEND_ID", "Command received: %d", command.command);
    
    if (command.command == CONNECTION_CONFIRMED) {
        LOG_I("HUNTER_SEND_ID", "Received CONNECTION_CONFIRMED from opponent");
        
        // Validate received match data
        if (command.match.getMatchId().empty()) {
            LOG_E("HUNTER_SEND_ID", "Received empty match ID");
            return;
        }
        if (command.match.getBountyId().empty()) {
            LOG_E("HUNTER_SEND_ID", "Received empty bounty ID");
            return;
        }
        
        LOG_I("HUNTER_SEND_ID", "Received match ID: %s, bounty ID: %s", 
                 command.match.getMatchId().c_str(), 
                 command.match.getBountyId().c_str());

        // Set opponent MAC address
        if (command.wifiMacAddr.empty()) {
            LOG_E("HUNTER_SEND_ID", "Received empty MAC address");
            return;
        }
        player->setOpponentMacAddress(command.wifiMacAddr);
        
        // Create new match with validated data
        Match* newMatch = MatchManager::GetInstance()->createMatch(
            command.match.getMatchId(),
            player->getUserID(),
            command.match.getBountyId()
        );
        
        if (!newMatch) {
            LOG_E("HUNTER_SEND_ID", "Failed to create match");
            return;
        }

        LOG_I("HUNTER_SEND_ID", "Created match with ID: %s", newMatch->getMatchId().c_str());
        
        try {
            QuickdrawWirelessManager::GetInstance()->broadcastPacket(
                *player->getOpponentMacAddress(),
                HUNTER_RECEIVE_MATCH,
                *newMatch
            );
            LOG_I("HUNTER_SEND_ID", "Sent HUNTER_RECEIVE_MATCH");
        } catch (const std::exception& e) {
            LOG_E("HUNTER_SEND_ID", "Failed to send HUNTER_RECEIVE_MATCH: %s", e.what());
        }
    } else if (command.command == BOUNTY_FINAL_ACK) {
        LOG_I("HUNTER_SEND_ID", "Received BOUNTY_FINAL_ACK from opponent");
        transitionToConnectionSuccessfulState = true;
    } else {
        LOG_W("HUNTER_SEND_ID", "Received unexpected command: %d", command.command);
    }
}


void HunterSendIdState::onStateLoop(Device *PDN) {}

void HunterSendIdState::onStateDismounted(Device *PDN) {
    LOG_I("HUNTER_SEND_ID", "State dismounted");
    transitionToConnectionSuccessfulState = false;
    BaseHandshakeState::resetTimeout();
    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

bool HunterSendIdState::transitionToConnectionSuccessful() {
    return transitionToConnectionSuccessfulState;
}
