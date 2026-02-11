#pragma once

#include <cstdint>
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
    }

protected:
    MiniGameOutcome outcome;

private:
    GameType gameType;
    const char* displayName;
};
