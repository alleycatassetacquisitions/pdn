#pragma once

#include <cstdint>
#include "state/state-machine-manager.hpp"
#include "device/device-types.hpp"

/*
 * MiniGame is the base class for all minigames in the ChallengeDevice system.
 * Each of the 7 minigames (Signal Echo, Ghost Runner, etc.) derives from MiniGame.
 *
 * MiniGame extends SwappableGame (which extends StateMachine) with:
 * - Game identity (GameType, display name)
 * - Rich outcome tracking (MiniGameOutcome with hardMode)
 * - Reset capability for replay without reconstructing the object
 *
 * Subclasses must implement populateStateMap() and their game-specific states.
 * States set `outcome` when the game ends (win or lose).
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

class MiniGame : public SwappableGame {
public:
    MiniGame(Device* device, GameType gameType, const char* displayName) :
      SwappableGame(device),
      gameType(gameType),
      displayName(displayName)
    {
    }

    virtual ~MiniGame() = default;

    virtual void populateStateMap() override = 0;

    // SwappableGame interface
    bool isReadyForResume() const override { return readyForResume; }
    SwappableOutcome getOutcome() const override {
        return {static_cast<int>(outcome.result), outcome.score, outcome.hardMode};
    }
    int getGameId() const override { return static_cast<int>(gameType); }

    // MiniGame-specific API
    GameType getGameType() const { return gameType; }
    const char* getDisplayName() const { return displayName; }

    const MiniGameOutcome& getMiniGameOutcome() const { return outcome; }
    bool isGameComplete() const { return outcome.isComplete(); }

    void setOutcome(const MiniGameOutcome& newOutcome) {
        outcome = newOutcome;
    }

    void setReadyForResume(bool ready) { readyForResume = ready; }

    virtual void resetGame() {
        outcome = MiniGameOutcome{};
        readyForResume = false;
    }

protected:
    MiniGameOutcome outcome;
    bool readyForResume = false;

private:
    GameType gameType;
    const char* displayName;
};
