#pragma once

#ifdef NATIVE_BUILD

#include <cstdio>
#include <string>
#include <vector>

#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include "device/drivers/peer-comms-types.hpp"

namespace cli {

/**
 * Snapshot of a single simulated device's driver and game state, gathered at
 * render time so the renderer formats a value struct instead of querying each
 * native driver directly.
 */
struct DeviceDiagnostics {
    std::string deviceId;
    bool isHunter = false;
    std::string userId;
    std::string allegiance;
    int wins = 0;
    int losses = 0;
    int streak = 0;
    int stateId = -1;
    std::string stateName;
    std::string stateHistory;

    std::string fontModeName;
    std::vector<std::string> displayRows;
    std::vector<std::string> asciiMirrorLines;

    std::vector<LEDState::SingleLEDState> leftLights;
    std::vector<LEDState::SingleLEDState> rightLights;
    LEDState::SingleLEDState transmitLight;
    uint8_t globalBrightness = 0;

    bool hapticsOn = false;
    uint8_t hapticsIntensity = 0;

    size_t outInputQueueSize = 0;
    size_t outOutputBufferSize = 0;
    std::string outConnLabel;
    std::string outSentSummary;
    std::string outRecvSummary;

    size_t inInputQueueSize = 0;
    size_t inOutputBufferSize = 0;
    std::string inConnLabel;
    std::string inSentSummary;
    std::string inRecvSummary;

    std::string peerCommsState;
    std::string macString;
    size_t peerCount = 0;
    size_t pendingPacketCount = 0;
    std::string espNowTxSummary;
    std::string espNowRxSummary;

    bool httpConnected = false;
    size_t httpPendingRequests = 0;
    bool mockServerOnline = false;
    std::string httpRequestsSummary;

    std::string errorSummary;
};

/**
 * Helper to truncate a diagnostic string to `maxLen` characters.
 */
inline std::string truncateDiag(const std::string& str, size_t maxLen) {
    if (str.length() <= maxLen) return str;
    return str.substr(0, maxLen - 2) + "..";
}

/**
 * Maps a `PktType` to its short CLI display label.
 */
inline std::string getDiagPacketTypeName(PktType type) {
    switch (type) {
        case PktType::kPlayerInfoBroadcast: return "INFO";
        case PktType::kQuickdrawCommand: return "CMD";
        case PktType::kDebugPacket: return "DBG";
        default: return "?" + std::to_string(static_cast<int>(type));
    }
}

/**
 * Summarizes the most recent entries of a serial driver history deque.
 */
inline std::string summarizeSerialHistory(const std::deque<std::string>& history) {
    if (history.empty()) return "(none)";
    std::string out;
    for (size_t i = 0; i < history.size() && i < 3; i++) {
        if (i > 0) out += ", ";
        out += truncateDiag(history[history.size() - 1 - i], 15);
    }
    return out;
}

/**
 * Summarizes the 3 most recent sent or received packets in `pktHistory`.
 */
inline std::string summarizePacketHistory(
    const std::deque<PacketHistoryEntry>& pktHistory, bool isSent) {
    std::string summary = "(none)";
    int count = 0;
    for (std::deque<PacketHistoryEntry>::const_reverse_iterator it = pktHistory.rbegin();
         it != pktHistory.rend() && count < 3; ++it) {
        if (it->isSent == isSent) {
            if (count == 0) summary.clear();
            if (count > 0) summary += ", ";
            summary += getDiagPacketTypeName(it->packetType);
            summary += (isSent ? "->" : "<-") +
                       truncateDiag(isSent ? it->dstMac : it->srcMac, 8);
            count++;
        }
    }
    return summary;
}

/**
 * Pulls a `DeviceDiagnostics` snapshot from `device` and the global native
 * brokers for the CLI renderer.
 */
inline DeviceDiagnostics gatherDiagnostics(const DeviceInstance& device, bool includeMirror) {
    DeviceDiagnostics diag;
    diag.deviceId = device.deviceId;
    diag.isHunter = device.isHunter;
    diag.userId = device.player->getUserID();
    diag.allegiance = device.player->getAllegianceString();
    diag.wins = device.player->getWins();
    diag.losses = device.player->getLosses();
    diag.streak = device.player->getStreak();

    State* currentState = device.getCurrentState();
    diag.stateId = currentState ? currentState->getStateId() : -1;
    diag.stateName = getStateName(diag.stateId);

    if (device.stateHistory.empty()) {
        diag.stateHistory = "(none)";
    } else {
        for (size_t i = 0; i < device.stateHistory.size(); i++) {
            if (i > 0) diag.stateHistory += " -> ";
            diag.stateHistory += getStateName(device.stateHistory[i]);
        }
    }

    diag.fontModeName = device.displayDriver->getFontModeName();
    diag.displayRows.assign(4, "");
    const std::deque<std::string>& textHistory = device.displayDriver->getTextHistory();
    for (size_t i = 0; i < textHistory.size() && i < 4; i++) {
        diag.displayRows[i] = truncateDiag(textHistory[i], 40);
    }
    if (includeMirror) {
        diag.asciiMirrorLines = device.displayDriver->renderToBraille();
    }

    const std::array<LEDState::SingleLEDState, 9>& left = device.lightDriver->getLeftLights();
    const std::array<LEDState::SingleLEDState, 9>& right = device.lightDriver->getRightLights();
    diag.leftLights.assign(left.begin(), left.end());
    diag.rightLights.assign(right.begin(), right.end());
    diag.transmitLight = device.lightDriver->getTransmitLight();
    diag.globalBrightness = device.lightDriver->getGlobalBrightness();

    diag.hapticsOn = device.hapticsDriver->isOn();
    diag.hapticsIntensity = device.hapticsDriver->getIntensity();

    diag.outInputQueueSize = device.serialOutDriver->getInputQueueSize();
    diag.outOutputBufferSize = device.serialOutDriver->getOutputBufferSize();
    diag.outSentSummary = summarizeSerialHistory(device.serialOutDriver->getSentHistory());
    diag.outRecvSummary = summarizeSerialHistory(device.serialOutDriver->getReceivedHistory());

    diag.inInputQueueSize = device.serialInDriver->getInputQueueSize();
    diag.inOutputBufferSize = device.serialInDriver->getOutputBufferSize();
    diag.inSentSummary = summarizeSerialHistory(device.serialInDriver->getSentHistory());
    diag.inRecvSummary = summarizeSerialHistory(device.serialInDriver->getReceivedHistory());

    const std::vector<CableConnection>& connections =
        SerialCableBroker::getInstance().getConnections();
    for (const CableConnection& conn : connections) {
        if (conn.deviceA == device.deviceIndex || conn.deviceB == device.deviceIndex) {
            bool isDeviceA = (conn.deviceA == device.deviceIndex);
            int otherDevice = isDeviceA ? conn.deviceB : conn.deviceA;
            JackType myJack = isDeviceA ? conn.jackA : conn.jackB;
            char labelBuf[32];
            snprintf(labelBuf, sizeof(labelBuf), " \033[32m<->[Dev%d]\033[0m", otherDevice);
            if (myJack == JackType::OUTPUT_JACK) {
                diag.outConnLabel = labelBuf;
            } else {
                diag.inConnLabel = labelBuf;
            }
        }
    }

    diag.peerCommsState = device.peerCommsDriver->getStateString();
    diag.macString = device.peerCommsDriver->getMacString();
    diag.peerCount = NativePeerBroker::getInstance().getPeerCount();
    diag.pendingPacketCount = NativePeerBroker::getInstance().getPendingPacketCount();

    const std::deque<PacketHistoryEntry>& pktHistory =
        device.peerCommsDriver->getPacketHistory();
    diag.espNowTxSummary = summarizePacketHistory(pktHistory, true);
    diag.espNowRxSummary = summarizePacketHistory(pktHistory, false);

    diag.httpConnected = device.httpClientDriver->isConnected();
    diag.httpPendingRequests = device.httpClientDriver->getPendingRequestCount();
    diag.mockServerOnline = !MockHttpServer::getInstance().isOffline();
    diag.httpRequestsSummary = "(none)";
    const std::deque<HttpRequestHistoryEntry>& httpHistory =
        device.httpClientDriver->getRequestHistory();
    if (!httpHistory.empty()) {
        diag.httpRequestsSummary.clear();
        int count = 0;
        for (std::deque<HttpRequestHistoryEntry>::const_reverse_iterator it = httpHistory.rbegin();
             it != httpHistory.rend() && count < 3; ++it, ++count) {
            if (count > 0) diag.httpRequestsSummary += ", ";
            diag.httpRequestsSummary += it->method + " " + truncateDiag(it->path, 20);
            if (it->success) {
                diag.httpRequestsSummary += " [OK]";
            } else {
                char codeBuf[16];
                snprintf(codeBuf, sizeof(codeBuf), " [%d]", it->statusCode);
                diag.httpRequestsSummary += codeBuf;
            }
        }
    }

    diag.errorSummary = "(none)";
    const std::deque<LogEntry>& logs = device.loggerDriver->getRecentLogs();
    int errorCount = 0;
    for (std::deque<LogEntry>::const_reverse_iterator it = logs.rbegin();
         it != logs.rend() && errorCount < 2; ++it) {
        if (it->level == LogLevel::ERROR) {
            if (errorCount == 0) diag.errorSummary.clear();
            if (errorCount > 0) diag.errorSummary += ", ";
            diag.errorSummary += truncateDiag(it->message, 25);
            errorCount++;
        }
    }

    return diag;
}

}  // namespace cli

#endif  // NATIVE_BUILD
