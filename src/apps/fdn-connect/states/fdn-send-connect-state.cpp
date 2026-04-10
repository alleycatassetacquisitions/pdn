#include "apps/fdn-connect/fdn-connect-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "FdnSendConnectState";

FdnSendConnectState::FdnSendConnectState(Player* player, RemoteDeviceCoordinator* rdc, FDNConnectWirelessManager* wm)
    : State(FDN_SEND_CONNECT), player(player), remoteDeviceCoordinator(rdc), wirelessManager(wm) {}

void FdnSendConnectState::onStateMounted(Device* PDN) {
    const uint8_t* mac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
    if (mac == nullptr) {
        LOG_W(TAG, "No peer MAC on OUTPUT_JACK — cannot send PDN_CONNECT");
        sent = true;
        return;
    }

    const std::string idStr = player->getUserID();
    char idBuf[PLAYER_ID_BUFFER_SIZE] = {};
    strncpy(idBuf, idStr.c_str(), PLAYER_ID_BUFFER_SIZE - 1);

    int result = wirelessManager->sendPdnConnect(mac, idBuf);
    if (result == 0) {
        LOG_I(TAG, "PDN_CONNECT sent for player %s", idBuf);
    } else {
        LOG_E(TAG, "Failed to send PDN_CONNECT (err %d)", result);
    }
    sent = true;
}

void FdnSendConnectState::onStateLoop(Device* PDN) {}

void FdnSendConnectState::onStateDismounted(Device* PDN) {
    sent = false;
}

bool FdnSendConnectState::transitionToAwaitSequence() {
    return sent;
}
