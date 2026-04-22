// #include "symbol-match/symbol-match-states.hpp"
// #include "game/quickdraw-resources.hpp"
// #include "device/device.hpp"
// #include "device/drivers/logger.hpp"

// static const char* TAG = "SymbolMatch";

// Selection::Selection(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
//                         SymbolWirelessManager* symbolWirelessManager) : ConnectState(remoteDeviceCoordinator, SELECTION) {
//     this->symbolManager = symbolManager;
//     this->symbolWirelessManager = symbolWirelessManager;
// }

// Selection::~Selection() {
//     symbolManager = nullptr;
//     symbolWirelessManager = nullptr;
// }

// void Selection::onStateMounted(Device *FDN) {
//     LOG_W(TAG, "Selection mounted");
//     symbolManager->getRefreshTimer()->invalidate();
//     symbolManager->refreshSymbols();
//     bufferTimer.setTimer(bufferInterval);

//     // Send SYMBOLS_REFRESHED to all known peers
//     for (SerialIdentifier port : {SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK}) {
//         const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
//         if (peerMac != nullptr) {
//             symbolWirelessManager->setMacPeer(peerMac);
//             symbolWirelessManager->sendPacket(SMCommand::SYMBOLS_REFRESHED, SymbolId::SYMBOL_A, port);
//         }
//     }

// }

// void Selection::onStateLoop(Device *FDN) {
//     if (bufferTimer.expired()) {
//         transitionToIdleState = true;
//     } else if (bufferTimer.isRunning()) {
//         if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
//             renderLoadingScreen(FDN->getDisplay());
//         }
//     }
// }

// void Selection::onStateDismounted(Device *FDN) {
//     bufferTimer.invalidate();
//     transitionToIdleState = false;

//     FDN->getDisplay()->invalidateScreen();
//     LOG_W(TAG, "Selection dismounted");

//     symbolManager->resetRefreshTimer();
// }

// bool Selection::transitionToIdle() {
//     return transitionToIdleState;
// }

// bool Selection::isPrimaryRequired() {
//     return true;
// }

// bool Selection::isAuxRequired() {
//     return true;
// }