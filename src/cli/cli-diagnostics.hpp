#pragma once

#ifdef NATIVE_BUILD

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "wireless/mac-functions.hpp"

namespace cli {

// How much of each history the device panel has room for; nothing older is copied.
constexpr size_t PANEL_DISPLAY_TEXT_ROWS = 4;
constexpr size_t PANEL_HISTORY_ENTRIES = 3;
constexpr size_t PANEL_ERROR_ENTRIES = 2;

/** One serial jack as the device panel shows it. */
struct JackDiagnostics {
    size_t inputQueueSize = 0;
    size_t outputBufferSize = 0;
    // Index of the device at the other end of the cable, -1 with nothing plugged in.
    int cabledDevice = -1;
};

/** One ESP-NOW packet a device sent or received. */
struct PacketDiagnostics {
    PktType packetType;
    // The far end: where a sent packet went, where a received one came from.
    std::string peerMac;
};

/** One HTTP request a device made. */
struct HttpRequestDiagnostics {
    std::string method;
    std::string path;
    int statusCode;
    bool success;
};

/** Everything the device panel shows about one simulated device. */
struct DeviceDiagnostics {
    std::string deviceId;
    bool isHunter = false;

    std::string userId;
    std::string allegiance;
    int wins = 0;
    int losses = 0;
    int streak = 0;

    int stateId = -1;
    // Oldest first.
    std::deque<int> stateHistory;

    std::string fontModeName;
    // Most recent first.
    std::vector<std::string> displayText;
    // Empty unless asked for: producing it walks every pixel of the display.
    std::vector<std::string> displayMirror;

    std::array<LEDState::SingleLEDState, 9> leftLights;
    std::array<LEDState::SingleLEDState, 9> rightLights;
    LEDState::SingleLEDState transmitLight;
    uint8_t globalBrightness = 0;

    bool hapticsOn = false;
    int hapticsIntensity = 0;

    JackDiagnostics outputJack;
    JackDiagnostics inputJack;

    PeerCommsState peerCommsState = PeerCommsState::DISABLED;
    std::string macAddress;
    size_t peerCount = 0;
    size_t pendingPacketCount = 0;
    // Most recent first.
    std::vector<PacketDiagnostics> sentPackets;
    std::vector<PacketDiagnostics> receivedPackets;

    bool httpConnected = false;
    size_t httpPendingRequestCount = 0;
    // Most recent first.
    std::vector<HttpRequestDiagnostics> httpRequests;

    // Most recent first.
    std::vector<std::string> recentErrors;
};

/** Reads one jack's buffer levels; which device it is cabled to comes from the broker. */
inline JackDiagnostics gatherJackDiagnostics(const NativeSerialDriver& jack) {
    JackDiagnostics diagnostics;
    diagnostics.inputQueueSize = jack.getInputQueueSize();
    diagnostics.outputBufferSize = jack.getOutputBufferSize();
    return diagnostics;
}

/**
 * Reads everything the panel shows about `device`. Also records the device's
 * current state in its state history, which therefore only advances for a
 * device that is being shown.
 */
inline DeviceDiagnostics gatherDiagnostics(DeviceInstance& device, bool includeDisplayMirror) {
    DeviceDiagnostics diagnostics;
    diagnostics.deviceId = device.deviceId;
    diagnostics.isHunter = device.isHunter;

    diagnostics.userId = device.player->getUserID();
    diagnostics.allegiance = device.player->getAllegianceString();
    diagnostics.wins = device.player->getWins();
    diagnostics.losses = device.player->getLosses();
    diagnostics.streak = device.player->getStreak();

    State* currentState = device.getCurrentState();
    diagnostics.stateId = currentState ? currentState->getStateId() : -1;
    device.updateStateHistory(diagnostics.stateId);
    diagnostics.stateHistory = device.stateHistory;

    diagnostics.fontModeName = device.displayDriver->getFontModeName();
    const std::deque<std::string>& textHistory = device.displayDriver->getTextHistory();
    for (size_t i = 0; i < textHistory.size() && i < PANEL_DISPLAY_TEXT_ROWS; i++) {
        diagnostics.displayText.push_back(textHistory[i]);
    }
    if (includeDisplayMirror) {
        diagnostics.displayMirror = device.displayDriver->renderToBraille();
    }

    diagnostics.leftLights = device.lightDriver->getLeftLights();
    diagnostics.rightLights = device.lightDriver->getRightLights();
    diagnostics.transmitLight = device.lightDriver->getTransmitLight();
    diagnostics.globalBrightness = device.lightDriver->getGlobalBrightness();

    diagnostics.hapticsOn = device.hapticsDriver->isOn();
    diagnostics.hapticsIntensity = device.hapticsDriver->getIntensity();

    diagnostics.outputJack = gatherJackDiagnostics(*device.serialOutDriver);
    diagnostics.inputJack = gatherJackDiagnostics(*device.serialInDriver);
    for (const CableConnection& connection : SerialCableBroker::getInstance().getConnections()) {
        if (connection.deviceA != device.deviceIndex && connection.deviceB != device.deviceIndex) {
            continue;
        }
        const bool isDeviceA = connection.deviceA == device.deviceIndex;
        const JackType jackType = isDeviceA ? connection.jackA : connection.jackB;
        JackDiagnostics& jack =
        jackType == JackType::OUTPUT_JACK ? diagnostics.outputJack : diagnostics.inputJack;
        jack.cabledDevice = isDeviceA ? connection.deviceB : connection.deviceA;
    }

    diagnostics.peerCommsState = device.peerCommsDriver->getPeerCommsState();
    diagnostics.macAddress = MacToString(device.peerCommsDriver->getMacAddress());
    diagnostics.peerCount = NativePeerBroker::getInstance().getPeerCount();
    diagnostics.pendingPacketCount = NativePeerBroker::getInstance().getPendingPacketCount();
    const std::deque<PacketHistoryEntry>& packetHistory =
    device.peerCommsDriver->getPacketHistory();
    for (std::deque<PacketHistoryEntry>::const_reverse_iterator it = packetHistory.rbegin();
         it != packetHistory.rend(); ++it) {
        std::vector<PacketDiagnostics>& packets =
        it->isSent ? diagnostics.sentPackets : diagnostics.receivedPackets;
        if (packets.size() < PANEL_HISTORY_ENTRIES) {
            packets.push_back({it->packetType, MacToString(it->isSent ? it->dstMac : it->srcMac)});
        }
    }

    diagnostics.httpConnected = device.httpClientDriver->isConnected();
    diagnostics.httpPendingRequestCount = device.httpClientDriver->getPendingRequestCount();
    const std::deque<HttpRequestHistoryEntry>& requestHistory =
    device.httpClientDriver->getRequestHistory();
    for (std::deque<HttpRequestHistoryEntry>::const_reverse_iterator it = requestHistory.rbegin();
         it != requestHistory.rend() && diagnostics.httpRequests.size() < PANEL_HISTORY_ENTRIES;
         ++it) {
        diagnostics.httpRequests.push_back({it->method, it->path, it->statusCode, it->success});
    }

    const std::deque<LogEntry>& logs = device.loggerDriver->getRecentLogs();
    for (std::deque<LogEntry>::const_reverse_iterator it = logs.rbegin();
         it != logs.rend() && diagnostics.recentErrors.size() < PANEL_ERROR_ENTRIES; ++it) {
        if (it->level == LogLevel::ERROR) {
            diagnostics.recentErrors.push_back(it->message);
        }
    }

    return diagnostics;
}

}  // namespace cli

#endif  // NATIVE_BUILD
