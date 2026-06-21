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

int ControllerWirelessManager::sendPacket(int command,
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

int ControllerWirelessManager::sendGameSelectPacket(GameSelectId gameId, SerialIdentifier serialPort) {
    GameSelectPacket packet{};
    packet.command = GAME_SELECT;
    packet.gameId = static_cast<int>(gameId);

    LOG_W(CWM_TAG,
          "TX game select command %d to %s (gameId=%d, targetPort=%d)",
          packet.command,
          MacToString(macPeer),
          packet.gameId,
          static_cast<int>(serialPort));

    return wirelessManager->sendEspNowData(
        macPeer,
        PktType::kGameSelect,
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(packet));
}

int ControllerWirelessManager::sendGameResponsePacket(GameResponseId responseId, SerialIdentifier serialPort) {
    GameResponsePacket packet{};
    packet.responseId = static_cast<int>(responseId);

    LOG_W(CWM_TAG,
          "TX game response %d to %s (targetPort=%d)",
          packet.responseId,
          MacToString(macPeer),
          static_cast<int>(serialPort));

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

    auto callbackIt = packetReceivedCallbacks.find(resolvedPort);
    if (callbackIt != packetReceivedCallbacks.end() && callbackIt->second) {
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
        LOG_W(CWM_TAG, "No matching port for game select packet from %s",
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    GameSelectCommand command(macAddress,
                            packet->command,
                            static_cast<GameSelectId>(packet->gameId));

    auto callbackIt = gameSelectReceivedCallbacks.find(resolvedPort);
    if (callbackIt != gameSelectReceivedCallbacks.end() && callbackIt->second) {
        callbackIt->second(command);
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
        LOG_W(CWM_TAG, "No matching port for game response packet from %s",
              macAddress ? MacToString(macAddress) : "(null)");
        return -1;
    }

    GameResponseCommand command(macAddress, static_cast<GameResponseId>(packet->responseId));

    auto callbackIt = gameResponseReceivedCallbacks.find(resolvedPort);
    if (callbackIt != gameResponseReceivedCallbacks.end() && callbackIt->second) {
        callbackIt->second(command);
    }

    return 1;
}

void ControllerWirelessManager::setPacketReceivedCallback(
    const std::function<void(const ControllerCommand&)>& callback, SerialIdentifier port) {
    packetReceivedCallbacks[port] = callback;
}

void ControllerWirelessManager::setGameSelectReceivedCallback(
    const std::function<void(const GameSelectCommand&)>& callback, SerialIdentifier port) {
    gameSelectReceivedCallbacks[port] = callback;
}

void ControllerWirelessManager::setGameResponseReceivedCallback(
    const std::function<void(const GameResponseCommand&)>& callback, SerialIdentifier port) {
    gameResponseReceivedCallbacks[port] = callback;
}

void ControllerWirelessManager::clearCallback() {
    packetReceivedCallbacks.clear();
    gameSelectReceivedCallbacks.clear();
    gameResponseReceivedCallbacks.clear();
}
