#include "id-generator.hpp"
#include "game/handshake-machine.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"

// Opening Handshake State for a Hunter. we have sent our mac address over serial and are waiting
// for the opponent to send the match id and their user id.

HunterSendIdState::HunterSendIdState(Player *player) : State(HUNTER_SEND_ID_STATE) {
    this->player = player;
}

HunterSendIdState::~HunterSendIdState() {
    player = nullptr;
    
}


void HunterSendIdState::onStateMounted(Device *PDN) {
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(std::bind(&HunterSendIdState::onQuickdrawCommandReceived, this, std::placeholders::_1));
}

void HunterSendIdState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (command.command == CONNECTION_CONFIRMED) {
        ESP_LOGI("HUNTER", "Received CONNECTION_CONFIRMED from opponent");
        player->setOpponentMacAddress(command.wifiMacAddr);
        player->setCurrentOpponentId(command.opponentId);
        player->setCurrentMatchId(command.matchId);
        QuickdrawWirelessManager::GetInstance()->broadcastPacket(*player->getOpponentMacAddress(), HUNTER_RECEIVE_MATCH, 0 /*drawTimeMs*/, 0 /*ackCount*/, *player->getCurrentMatchId(), *player->getCurrentOpponentId());
        transitionToHunterSendAckState = true;
    }
}


void HunterSendIdState::onStateLoop(Device *PDN) {}

void HunterSendIdState::onStateDismounted(Device *PDN) {
    transitionToHunterSendAckState = false;
    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

bool HunterSendIdState::transitionToSendAck() {
    return transitionToHunterSendAckState;
}
