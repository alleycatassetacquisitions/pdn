#pragma once

#include <cstdint>
#include <cstdlib>
#include "state/state-machine.hpp"
#include "device/device-types.hpp"

/*
 * MiniGame is the base class for all minigames in the FDN system.
 * Each minigame (Signal Echo, Ghost Runner, etc.) derives from MiniGame.
 *
 * MiniGame extends StateMachine with:
 * - Game identity (GameType, display name)
 * - Outcome tracking (MiniGameOutcome)
 * - Configure/reset pattern for replayability without reconstruction
 *
 * Terminal states call Device::returnToPreviousApp() in managed mode,
 * or loop back for standalone replay.
 */

enum class MiniGameResult : uint8_t {
    IN_PROGRESS = 0,
    WON = 1,
    LOST = 2,
};

struct MiniGameOutcome {
    MiniGameResult result = MiniGameResult::IN_PROGRESS;
    int score = 0;
    bool hardMode = false;
    uint32_t startTimeMs = 0;  // Game start time (from millis())
    bool isComplete() const { return result != MiniGameResult::IN_PROGRESS; }
};

class MiniGame : public StateMachine {
public:
    MiniGame(int stateId, GameType gameType, const char* displayName) :
      StateMachine(stateId),
      gameType(gameType),
      displayName(displayName)
    {
    }

    virtual ~MiniGame() = default;

    GameType getGameType() const { return gameType; }
    const char* getDisplayName() const { return displayName; }

    const MiniGameOutcome& getOutcome() const { return outcome; }
    bool isGameComplete() const { return outcome.isComplete(); }

    void setOutcome(const MiniGameOutcome& newOutcome) {
        outcome = newOutcome;
    }

    virtual void resetGame() {
        outcome = MiniGameOutcome{};
        // Note: startTimeMs will be set by intro states via setStartTime()
    }

    void setStartTime(uint32_t timeMs) {
        outcome.startTimeMs = timeMs;
    }

    /*
     * Seed the PRNG. Should be called once during initialization (e.g., in populateStateMap).
     * If the minigame config has a nonzero rngSeed, uses that seed (deterministic for tests).
     * Otherwise uses 0 (production would use MAC address or time-based, but standalone falls back to 0).
     */
    void seedRng(unsigned long rngSeed) {
        if (rngSeed != 0) {
            srand(static_cast<unsigned int>(rngSeed));
        } else {
            srand(static_cast<unsigned int>(0));
        }
    }

protected:
    MiniGameOutcome outcome;

private:
    GameType gameType;
    const char* displayName;
};
