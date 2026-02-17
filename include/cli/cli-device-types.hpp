#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <deque>
#include "device/device-types.hpp"
#include "cli/cli-duel.hpp"

// Forward declarations — DeviceInstance only holds pointers to these
class NativeClockDriver;
class NativeLoggerDriver;
class NativeDisplayDriver;
class NativeButtonDriver;
class NativeLightStripDriver;
class NativeHapticsDriver;
class NativeSerialDriver;
class NativeHttpClientDriver;
class NativePeerCommsDriver;
class NativePrefsDriver;
class PDN;
class Player;
class StateMachine;
class QuickdrawWirelessManager;

namespace cli {

/**
 * Structure to hold all components for a single simulated PDN device.
 */
struct DeviceInstance {
    int deviceIndex;
    std::string deviceId;  // e.g., "0010", "0011", etc.
    bool isHunter;
    DeviceType deviceType = DeviceType::PLAYER;
    GameType gameType = GameType::QUICKDRAW;

    // Native drivers
    NativeClockDriver* clockDriver = nullptr;
    NativeLoggerDriver* loggerDriver = nullptr;
    NativeDisplayDriver* displayDriver = nullptr;
    NativeButtonDriver* primaryButtonDriver = nullptr;
    NativeButtonDriver* secondaryButtonDriver = nullptr;
    NativeLightStripDriver* lightDriver = nullptr;
    NativeHapticsDriver* hapticsDriver = nullptr;
    NativeSerialDriver* serialOutDriver = nullptr;
    NativeSerialDriver* serialInDriver = nullptr;
    NativeHttpClientDriver* httpClientDriver = nullptr;
    NativePeerCommsDriver* peerCommsDriver = nullptr;
    NativePrefsDriver* storageDriver = nullptr;

    // Game objects
    PDN* pdn = nullptr;
    Player* player = nullptr;
    StateMachine* game = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;

    // State history (circular buffer, most recent at back)
    std::deque<int> stateHistory;
    int lastStateId = -1;

    // Duel tracking
    DuelHistory duelHistory;
    SeriesState seriesState;
    bool rematchPending = false;
    bool duelRecordedThisSession = false;  // Prevents duplicate recording

    /**
     * Track state transitions for display in the UI.
     */
    void updateStateHistory(int currentStateId) {
        if (currentStateId != lastStateId) {
            stateHistory.push_back(currentStateId);
            while (stateHistory.size() > 4) {
                stateHistory.pop_front();
            }
            lastStateId = currentStateId;
        }
    }
};

// State name getters — implementations in src/cli/cli-state-names.cpp
const char* getQuickdrawStateName(int stateId);
const char* getSignalEchoStateName(int stateId);
const char* getFirewallDecryptStateName(int stateId);
const char* getGhostRunnerStateName(int stateId);
const char* getSpikeVectorStateName(int stateId);
const char* getCipherPathStateName(int stateId);
const char* getExploitSequencerStateName(int stateId);
const char* getBreachDefenseStateName(int stateId);
const char* getFdnStateName(int stateId);
const char* getStateName(int stateId, DeviceType deviceType = DeviceType::PLAYER, GameType gameType = GameType::QUICKDRAW);

} // namespace cli

#endif // NATIVE_BUILD
