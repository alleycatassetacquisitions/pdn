#pragma once

#include "state/state.hpp"
#include "device/device-types.hpp"
#include "device/device-constants.hpp"
#include "utils/simple-timer.hpp"
#include "game/fdn-result-manager.hpp"
#include <string>
#include <vector>

class FdnGame;

enum FdnStateId {
    NPC_IDLE = 0,
    NPC_HANDSHAKE = 1,
    NPC_GAME_ACTIVE = 2,
    NPC_RECEIVE_RESULT = 3,
};

class NpcIdle : public State {
public:
    explicit NpcIdle(FdnGame* game);
    ~NpcIdle() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToHandshake();

private:
    FdnGame* game;
    SimpleTimer broadcastTimer;
    static constexpr int BROADCAST_INTERVAL_MS = 500;
    bool transitionToHandshakeState = false;

    void serialEventCallback(const std::string& message);
    void renderDisplay(Device* PDN);
};

class NpcHandshake : public State {
public:
    explicit NpcHandshake(FdnGame* game);
    ~NpcHandshake() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToGameActive();
    bool transitionToIdle();

private:
    FdnGame* game;
    SimpleTimer timeoutTimer;
    static constexpr int HANDSHAKE_TIMEOUT_MS = 10000;
    bool transitionToGameActiveState = false;
    bool transitionToIdleState = false;
    std::string playerMacAddress;
};

class NpcGameActive : public State {
public:
    explicit NpcGameActive(FdnGame* game);
    ~NpcGameActive() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToReceiveResult();
    bool transitionToIdle();

private:
    FdnGame* game;
    SimpleTimer inactivityTimer;
    static constexpr int INACTIVITY_TIMEOUT_MS = 30000;
    bool transitionToReceiveResultState = false;
    bool transitionToIdleState = false;

    int currentRound = 0;
    int totalRounds = 3;
    int sequenceLength = 4;
    int inputIndex = 0;
    int playerLives = 3;
    int playerScore = 0;
    bool gameComplete = false;
    bool playerWon = false;
    std::vector<bool> currentSequence;

    void generateSequence();
    void sendSequence(Device* PDN);
    void onEspNowReceived(const std::string& message, Device* PDN);
    void renderDisplay(Device* PDN);
};

class NpcReceiveResult : public State {
public:
    NpcReceiveResult(FdnGame* game, FdnResultManager* resultManager);
    ~NpcReceiveResult() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIdle();

private:
    FdnGame* game;
    FdnResultManager* resultManager;
    SimpleTimer displayTimer;
    static constexpr int DISPLAY_DURATION_MS = 3000;
    bool transitionToIdleState = false;
};
