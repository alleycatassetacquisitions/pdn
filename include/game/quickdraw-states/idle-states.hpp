#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "game/match-manager.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include <string>

// Forward declarations
class ProgressManager;

/*
 * Sleep - Dormant state with breathing LED animation
 */
class Sleep : public State {
public:
    explicit Sleep(Player* player);
    ~Sleep();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToAwakenSequence();

private:
    bool transitionToAwakenSequenceState = false;
    SimpleTimer dormantTimer;
    Player* player;
    bool breatheUp = true;
    int ledBrightness = 0;
    float pwm_val = 0.0;
    static constexpr int smoothingPoints = 255;
    static constexpr unsigned long SLEEP_DURATION = 60000UL;
};

/*
 * AwakenSequence - Transition from sleep to active state
 */
class AwakenSequence : public State {
public:
    explicit AwakenSequence(Player* player);
    ~AwakenSequence();
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToIdle();

private:
    static constexpr int AWAKEN_THRESHOLD = 20;
    SimpleTimer activationSequenceTimer;
    int activateMotorCount = 0;
    bool activateMotor = false;
    const int activationStepDuration = 100;
    Player* player;
};

/*
 * Idle - Main waiting state for player interactions
 */
class Idle : public State {
public:
    Idle(Player *player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager, ProgressManager* progressManager);
    ~Idle();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToHandshake();
    bool isFdnDetected() const { return fdnDetected; }
    bool transitionToColorPicker() const { return colorPickerRequested; }
    void cycleStats(Device *PDN);

private:
    Player *player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    ProgressManager* progressManager;
    SimpleTimer heartbeatTimer;
    SimpleTimer unsyncedIndicatorTimer;
    const int HEARTBEAT_INTERVAL_MS = 250;
    const int UNSYNCED_INDICATOR_INTERVAL_MS = 10000;
    bool transitionToHandshakeState = false;
    bool sendMacAddress = false;
    bool waitingForMacAddress = false;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int statsCount = 5;

    // FDN detection
    bool fdnDetected = false;
    std::string lastFdnMessage;

    // Color profile picker
    bool colorPickerRequested = false;

    void serialEventCallbacks(const std::string& message);
    void ledAnimation(Device *PDN);
};
