#include "wireless/controller-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include <cstring>

static const char* CWM_TAG = "CWM";

ControllerWirelessManager::ControllerWirelessManager() {
    std::memset(macPeer, 0, sizeof(macPeer));
    remoteDeviceCoordinator = nullptr;
    wirelessManager = nullptr;
}

ControllerWirelessManager::~ControllerWirelessManager() {
    wirelessManager = nullptr;
    remoteDeviceCoordinator = nullptr;
}

void ControllerWirelessManager::initialize(WirelessManager* wm, RemoteDeviceCoordinator* rdc) {
    wirelessManager = wm;
    remoteDeviceCoordinator = rdc;
}

void ControllerWirelessManager::setMacPeer(const uint8_t* macAddress) {
    memcpy(macPeer, macAddress, 6);
}

int ControllerWirelessManager::sendControllerCommandPacket(int command,
                                          ButtonIdentifier buttonId,
                                          ButtonInteraction interactionId,
                                          SerialIdentifier serialPort) {
    ControllerPacket packet{};
    packet.command = command;
    packet.buttonId = buttonId;
    packet.interactionId = interactionId;

    LOG_W(CWM_TAG,
          "TX controller command %d to %s (button=%d, interaction=%d, targetPort=%d)",
          command,
          MacToString(macPeer),
          static_cast<int>(buttonId),
          static_cast<int>(interactionId),
          static_cast<int>(serialPort));

    return wirelessManager->sendEspNowData(
        macPeer,
        PktType::kControllerCommand,
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(packet));
}

int ControllerWirelessManager::sendGameSelectPacket(GameSelectId gameId) {
    GameSelectPacket packet{};
    packet.command = GAME_SELECT;
    packet.gameId = static_cast<int>(gameId);

    LOG_W(CWM_TAG,
          "TX game select command %d to %s (gameId=%d)",
          packet.command,
          MacToString(macPeer),
          packet.gameId);

    return wirelessManager->sendEspNowData(
        macPeer,
        PktType::kGameSelect,
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(packet));
}

int ControllerWirelessManager::sendGameResponsePacket(GameResponseId responseId) {
    GameResponsePacket packet{};
    packet.responseId = static_cast<int>(responseId);

    LOG_W(CWM_TAG,
          "TX game response %d to %s",
          packet.responseId,
          MacToString(macPeer));

    return wirelessManager->sendEspNowData(
        macPeer,
        PktType::kGameResponse,
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(packet));
}

int ControllerWirelessManager::processControllerCommand(const uint8_t* macAddress,
                                                        const uint8_t* data,
                                                        size_t dataLen) {
    if (dataLen != sizeof(ControllerPacket)) {
        LOG_E(CWM_TAG, "RX controller pkt bad len: got %u expected %u from %s",
              static_cast<unsigned>(dataLen),
              static_cast<unsigned>(sizeof(ControllerPacket)),
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    const ControllerPacket* packet = reinterpret_cast<const ControllerPacket*>(data);

    LOG_W(CWM_TAG,
          "RX controller command %d from %s (button=%d, interaction=%d)",
          packet->command,
          MacToString(macAddress),
          static_cast<int>(packet->buttonId),
          static_cast<int>(packet->interactionId));

    if (remoteDeviceCoordinator == nullptr) {
        LOG_E(CWM_TAG, "RemoteDeviceCoordinator unavailable, dropping controller packet");
        return -1;
    }

    SerialIdentifier resolvedPort = SerialIdentifier::OUTPUT_JACK;
    bool portResolved = false;

    for (SerialIdentifier port : {SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK}) {
        PortState portState = remoteDeviceCoordinator->getPortState(port);
        for (const auto& peerMac : portState.peerMacAddresses) {
            if (macAddress != nullptr && std::memcmp(peerMac.data(), macAddress, 6) == 0) {
                resolvedPort = port;
                portResolved = true;
                break;
            }
        }
        if (portResolved) break;
    }

    if (!portResolved) {
        LOG_W(CWM_TAG, "No matching port for controller packet from %s",
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    ControllerCommand command(macAddress,
                            packet->command,
                            packet->buttonId,
                            packet->interactionId);

    auto callbackIt = controllerCommandReceivedCallbacks.find(resolvedPort);
    if (callbackIt != controllerCommandReceivedCallbacks.end() && callbackIt->second) {
        callbackIt->second(command);
    }

    return 1;
}

int ControllerWirelessManager::processGameSelectCommand(const uint8_t* macAddress,
                                                        const uint8_t* data,
                                                        size_t dataLen) {
    if (dataLen != sizeof(GameSelectPacket)) {
        LOG_E(CWM_TAG, "RX game select pkt bad len: got %u expected %u from %s",
              static_cast<unsigned>(dataLen),
              static_cast<unsigned>(sizeof(GameSelectPacket)),
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    const GameSelectPacket* packet = reinterpret_cast<const GameSelectPacket*>(data);

    if (packet->command < 0 || packet->command >= GAME_SELECT_CMD_COUNT) {
        LOG_E(CWM_TAG, "Invalid game select command %d from %s, dropping",
              packet->command,
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    if (packet->gameId < 0 || packet->gameId >= GAME_SELECT_ID_COUNT) {
        LOG_E(CWM_TAG, "Invalid game select id %d from %s, dropping",
              packet->gameId,
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    LOG_W(CWM_TAG,
          "RX game select command %d from %s (gameId=%d)",
          packet->command,
          MacToString(macAddress),
          packet->gameId);

    if (remoteDeviceCoordinator == nullptr) {
        LOG_E(CWM_TAG, "RemoteDeviceCoordinator unavailable, dropping game select packet");
        return -1;
    }

    if (!remoteDeviceCoordinator->isDirectPeer(macAddress)) {
        LOG_W(CWM_TAG, "Game select packet from unknown peer %s, dropping",
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    GameSelectCommand command(macAddress,
                            packet->command,
                            static_cast<GameSelectId>(packet->gameId));

    if (gameSelectReceivedCallback) {
        gameSelectReceivedCallback(command);
    }

    return 1;
}

int ControllerWirelessManager::processGameResponseCommand(const uint8_t* macAddress,
                                                          const uint8_t* data,
                                                          size_t dataLen) {
    if (dataLen != sizeof(GameResponsePacket)) {
        LOG_E(CWM_TAG, "RX game response pkt bad len: got %u expected %u from %s",
              static_cast<unsigned>(dataLen),
              static_cast<unsigned>(sizeof(GameResponsePacket)),
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    const GameResponsePacket* packet = reinterpret_cast<const GameResponsePacket*>(data);

    if (packet->responseId < 0 || packet->responseId >= GAME_RESPONSE_ID_COUNT) {
        LOG_E(CWM_TAG, "Invalid game response id %d from %s, dropping",
              packet->responseId,
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    LOG_W(CWM_TAG,
          "RX game response %d from %s",
          packet->responseId,
          MacToString(macAddress));

    if (remoteDeviceCoordinator == nullptr) {
        LOG_E(CWM_TAG, "RemoteDeviceCoordinator unavailable, dropping game response packet");
        return -1;
    }

    if (!remoteDeviceCoordinator->isDirectPeer(macAddress)) {
        LOG_W(CWM_TAG, "Game response packet from unknown peer %s, dropping",
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    GameResponseCommand command(macAddress, static_cast<GameResponseId>(packet->responseId));

    if (gameResponseReceivedCallback) {
        gameResponseReceivedCallback(command);
    }

    return 1;
}

void ControllerWirelessManager::setControllerCommandReceivedCallback(
    const std::function<void(const ControllerCommand&)>& callback, SerialIdentifier port) {
    controllerCommandReceivedCallbacks[port] = callback;
}

void ControllerWirelessManager::setGameSelectReceivedCallback(
    const std::function<void(const GameSelectCommand&)>& callback) {
    gameSelectReceivedCallback = callback;
}

void ControllerWirelessManager::setGameResponseReceivedCallback(
    const std::function<void(const GameResponseCommand&)>& callback) {
    gameResponseReceivedCallback = callback;
}

void ControllerWirelessManager::clearCallback() {
    controllerCommandReceivedCallbacks.clear();
    gameSelectReceivedCallback = nullptr;
    gameResponseReceivedCallback = nullptr;
    peripheralCommandReceivedCallback = nullptr;
}

int ControllerWirelessManager::sendPeripheralCommandPacket(
    PeripheralCmd command, uint8_t param1, uint8_t param2) {
    PeripheralCommandPacket packet{command, param1, param2};

    LOG_W(CWM_TAG,
          "TX peripheral command %d to %s (p1=%u p2=%u)",
          static_cast<int>(command),
          MacToString(macPeer),
          param1, param2);

    return wirelessManager->sendEspNowData(
        macPeer,
        PktType::kPeripheralCommand,
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(packet));
}

int ControllerWirelessManager::processPeripheralCommand(
    const uint8_t* macAddress, const uint8_t* data, size_t dataLen) {
    if (dataLen < sizeof(PeripheralCommandPacket)) {
        LOG_E(CWM_TAG, "RX peripheral pkt bad len: got %u expected %u",
              static_cast<unsigned>(dataLen),
              static_cast<unsigned>(sizeof(PeripheralCommandPacket)));
        return -1;
    }

    const auto* packet = reinterpret_cast<const PeripheralCommandPacket*>(data);

    LOG_W(CWM_TAG, "RX peripheral command %d from %s (p1=%u p2=%u)",
          static_cast<int>(packet->command),
          macAddress ? MacToString(macAddress) : "(null)",
          packet->param1, packet->param2);

    if (peripheralCommandReceivedCallback) {
        peripheralCommandReceivedCallback(packet->command, packet->param1, packet->param2);
    }
    return 1;
}

void ControllerWirelessManager::setPeripheralCommandReceivedCallback(
    const std::function<void(PeripheralCmd, uint8_t, uint8_t)>& callback) {
    peripheralCommandReceivedCallback = callback;
}

void ControllerWirelessManager::clearPeripheralCallback() {
    peripheralCommandReceivedCallback = nullptr;
}
