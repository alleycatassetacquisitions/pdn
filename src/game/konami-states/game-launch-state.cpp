#include "game/konami-states/game-launch-state.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/difficulty-scaler.hpp"
#include "state/state-machine.hpp"
#include "game/minigame.hpp"

static const char* TAG = "GameLaunchState";

GameLaunchState::GameLaunchState(int stateId, GameType gameType, LaunchMode mode) :
    State(stateId),
    gameType(gameType),
    launchMode(mode),
    gameLaunched(false),
    gameResumed(false),
    transitionToButtonAwardedState(false),
    transitionToBoonAwardedState(false),
    transitionToGameOverState(false),
    playerWon(false)
{
}

GameLaunchState::~GameLaunchState() {
}

void GameLaunchState::onStateMounted(Device* PDN) {
    gameLaunched = false;
    gameResumed = false;
    transitionToButtonAwardedState = false;
    transitionToBoonAwardedState = false;
    transitionToGameOverState = false;
    playerWon = false;

    LOG_I(TAG, "GameLaunchState mounted - game=%d, mode=%d",
          static_cast<int>(gameType), static_cast<int>(launchMode));

    // Display game name and mode
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText(getGameDisplayName(gameType), 10, 20);
    PDN->getDisplay()->drawText(getModeDisplayText(), 10, 35);
    PDN->getDisplay()->drawText("LAUNCHING...", 10, 50);
    PDN->getDisplay()->render();

    displayTimer.setTimer(DISPLAY_DURATION_MS);
}

void GameLaunchState::onStateLoop(Device* PDN) {
    displayTimer.updateTime();

    // Wait for display duration before launching
    if (!gameLaunched && displayTimer.expired()) {
        launchGame(PDN);
        gameLaunched = true;
    }

    // After game resumes, check outcome and transition
    if (gameResumed) {
        readOutcome(PDN);

        // Route based on mode and outcome
        switch (launchMode) {
            case LaunchMode::EASY_FIRST_ENCOUNTER:
                if (playerWon) {
                    transitionToButtonAwardedState = true;
                } else {
                    transitionToGameOverState = true;
                }
                break;

            case LaunchMode::HARD_FIRST_ENCOUNTER:
                if (playerWon) {
                    transitionToBoonAwardedState = true;
                } else {
                    transitionToGameOverState = true;
                }
                break;

            case LaunchMode::EASY_REPLAY:
            case LaunchMode::MASTERY_REPLAY:
                // No rewards on replay
                transitionToGameOverState = true;
                break;
        }
    }
}

void GameLaunchState::onStateDismounted(Device* PDN) {
    gameLaunched = false;
    gameResumed = false;
    transitionToButtonAwardedState = false;
    transitionToBoonAwardedState = false;
    transitionToGameOverState = false;
    playerWon = false;
    displayTimer.invalidate();
}

std::unique_ptr<Snapshot> GameLaunchState::onStatePaused(Device* PDN) {
    auto snapshot = std::make_unique<GameLaunchSnapshot>();
    snapshot->gameLaunched = gameLaunched;
    snapshot->gameResumed = gameResumed;
    return snapshot;
}

void GameLaunchState::onStateResumed(Device* PDN, Snapshot* snapshot) {
    if (snapshot) {
        auto* gameLaunchSnapshot = static_cast<GameLaunchSnapshot*>(snapshot);
        if (gameLaunchSnapshot) {
            gameLaunched = gameLaunchSnapshot->gameLaunched;
            gameResumed = true;  // Mark as resumed so we can check outcome
            LOG_I(TAG, "GameLaunchState resumed - checking outcome");
        }
    }
}

bool GameLaunchState::transitionToButtonAwarded() {
    return transitionToButtonAwardedState;
}

bool GameLaunchState::transitionToBoonAwarded() {
    return transitionToBoonAwardedState;
}

bool GameLaunchState::transitionToGameOver() {
    return transitionToGameOverState;
}

void GameLaunchState::launchGame(Device* PDN) {
    LOG_I(TAG, "Launching game %d with mode %d",
          static_cast<int>(gameType), static_cast<int>(launchMode));

    // Get the app ID for the game type
    int appId;
    switch (gameType) {
        case GameType::SIGNAL_ECHO:       appId = 1; break;
        case GameType::GHOST_RUNNER:      appId = 2; break;
        case GameType::SPIKE_VECTOR:      appId = 3; break;
        case GameType::FIREWALL_DECRYPT:  appId = 4; break;
        case GameType::CIPHER_PATH:       appId = 5; break;
        case GameType::EXPLOIT_SEQUENCER: appId = 6; break;
        case GameType::BREACH_DEFENSE:    appId = 7; break;
        default:                          appId = 1; break;
    }

    // Launch the app
    PDN->setActiveApp(StateId(appId));
}

void GameLaunchState::readOutcome(Device* PDN) {
    // Get the completed app
    int appId;
    switch (gameType) {
        case GameType::SIGNAL_ECHO:       appId = 1; break;
        case GameType::GHOST_RUNNER:      appId = 2; break;
        case GameType::SPIKE_VECTOR:      appId = 3; break;
        case GameType::FIREWALL_DECRYPT:  appId = 4; break;
        case GameType::CIPHER_PATH:       appId = 5; break;
        case GameType::EXPLOIT_SEQUENCER: appId = 6; break;
        case GameType::BREACH_DEFENSE:    appId = 7; break;
        default:                          appId = 1; break;
    }

    StateMachine* stateMachineApp = PDN->getApp(StateId(appId));
    MiniGame* completedApp = static_cast<MiniGame*>(stateMachineApp);
    if (completedApp) {
        const MiniGameOutcome& outcome = completedApp->getOutcome();
        playerWon = (outcome.result == MiniGameResult::WON);
        LOG_I(TAG, "Game outcome: %s", playerWon ? "WIN" : "LOSS");
    } else {
        LOG_W(TAG, "Could not read outcome from app %d", appId);
        playerWon = false;
    }
}

const char* GameLaunchState::getModeDisplayText() {
    switch (launchMode) {
        case LaunchMode::EASY_FIRST_ENCOUNTER: return "[ EASY MODE ]";
        case LaunchMode::EASY_REPLAY:          return "[ EASY REPLAY ]";
        case LaunchMode::HARD_FIRST_ENCOUNTER: return "[ HARD MODE ]";
        case LaunchMode::MASTERY_REPLAY:       return "[ MASTERY ]";
        default:                               return "[ UNKNOWN ]";
    }
}
