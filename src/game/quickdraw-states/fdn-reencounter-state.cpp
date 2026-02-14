#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"
#include <cstdint>
#include <cstdio>

static const char* TAG = "FdnReencounter";

FdnReencounter::FdnReencounter(Player* player) :
    State(FDN_REENCOUNTER),
    player(player)
{
}

FdnReencounter::~FdnReencounter() {
    player = nullptr;
}

void FdnReencounter::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    transitionToFdnCompleteState = false;
    pendingLaunch = false;
    gameLaunched = false;
    cursorIndex = 0;

    // Read game data from Player (set by FdnDetected)
    pendingGameType = static_cast<GameType>(player->getLastFdnGameType());
    pendingReward = static_cast<KonamiButton>(player->getLastFdnReward());

    // Check progression to determine prompt style
    bool hasButton = player->hasUnlockedButton(static_cast<uint8_t>(pendingReward));
    bool hasProfile = player->hasColorProfileEligibility(static_cast<int>(pendingGameType));
    fullyCompleted = (hasButton && hasProfile);

    LOG_I(TAG, "Re-encounter: %s, fullyCompleted=%d",
           getGameDisplayName(pendingGameType), fullyCompleted);

    // Button callbacks: PRIMARY(UP) = confirm, SECONDARY(DOWN) = cycle
    parameterizedCallbackFunction confirmCb = [](void* ctx) {
        auto* self = static_cast<FdnReencounter*>(ctx);
        if (self->cursorIndex == 2) {
            // SKIP
            LOG_I(TAG, "Player chose SKIP");
            self->transitionToIdleState = true;
        } else {
            // HARD (0) or EASY (1) -- flag for launch in onStateLoop
            self->pendingLaunch = true;
        }
    };

    parameterizedCallbackFunction cycleCb = [](void* ctx) {
        auto* self = static_cast<FdnReencounter*>(ctx);
        self->cursorIndex = (self->cursorIndex + 1) % OPTION_COUNT;
    };

    PDN->getPrimaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(cycleCb, this, ButtonInteraction::CLICK);

    // Auto-dismiss timer
    timeoutTimer.setTimer(TIMEOUT_MS);

    renderUi(PDN);
}

void FdnReencounter::onStateLoop(Device* PDN) {
    timeoutTimer.updateTime();

    if (pendingLaunch) {
        pendingLaunch = false;
        launchGame(PDN);
        return;
    }

    if (timeoutTimer.expired()) {
        LOG_I(TAG, "Auto-dismiss (SKIP)");
        transitionToIdleState = true;
    }
}

void FdnReencounter::onStateDismounted(Device* PDN) {
    timeoutTimer.invalidate();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

std::unique_ptr<Snapshot> FdnReencounter::onStatePaused(Device* PDN) {
    auto snap = std::make_unique<FdnReencounterSnapshot>();
    snap->gameLaunched = gameLaunched;
    return snap;
}

void FdnReencounter::onStateResumed(Device* PDN, Snapshot* snapshot) {
    if (snapshot) {
        auto* reencounterSnap = static_cast<FdnReencounterSnapshot*>(snapshot);
        gameLaunched = reencounterSnap->gameLaunched;
    }
    // Minigame is done -- transition to FdnComplete
    if (gameLaunched) {
        transitionToFdnCompleteState = true;
    }
}

bool FdnReencounter::transitionToIdle() {
    return transitionToIdleState;
}

bool FdnReencounter::transitionToFdnComplete() {
    return transitionToFdnCompleteState;
}

void FdnReencounter::renderUi(Device* PDN) {
    if (!PDN) return;

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    PDN->getDisplay()->drawText(getGameDisplayName(pendingGameType), 10, 12);

    if (fullyCompleted) {
        PDN->getDisplay()->drawText("COMPLETED!", 10, 24);
    } else {
        PDN->getDisplay()->drawText("BEATEN EASY", 10, 24);
    }

    // Options with cursor
    const char* options[3] = {"HARD", "EASY", "SKIP"};
    for (int i = 0; i < OPTION_COUNT; i++) {
        char line[32];
        if (i == cursorIndex) {
            snprintf(line, sizeof(line), "> %s", options[i]);
        } else {
            snprintf(line, sizeof(line), "  %s", options[i]);
        }
        int y = 36 + (i * 10);
        PDN->getDisplay()->drawText(line, 10, y);
    }

    PDN->getDisplay()->drawText("UP:ok DOWN:next", 5, 60);
    PDN->getDisplay()->render();
}

void FdnReencounter::launchGame(Device* PDN) {
    bool hardMode = (cursorIndex == 0);

    // Determine if this is recreational play
    bool recreational = false;
    if (fullyCompleted) {
        // Both button + profile already unlocked -- always recreational
        recreational = true;
    } else if (cursorIndex == 1) {
        // Chose EASY on a re-encounter (already have button) -- recreational
        recreational = true;
    }
    // cursorIndex == 0 (HARD) on non-fully-completed = earning profile, not recreational

    player->setRecreationalMode(recreational);

    LOG_I(TAG, "Launching %s (%s, %s)",
           getGameDisplayName(pendingGameType),
           hardMode ? "HARD" : "EASY",
           recreational ? "recreational" : "progression");

    // Increment attempt counter based on difficulty
    if (hardMode) {
        player->incrementHardAttempts(pendingGameType);
    } else {
        player->incrementEasyAttempts(pendingGameType);
    }

    // Look up the app
    int appId = getAppIdForGame(pendingGameType);
    if (appId < 0) {
        LOG_W(TAG, "No app registered for game type %d", static_cast<int>(pendingGameType));
        transitionToIdleState = true;
        return;
    }

    auto* game = static_cast<MiniGame*>(PDN->getApp(StateId(appId)));
    if (!game) {
        LOG_W(TAG, "App %d not found in AppConfig", appId);
        transitionToIdleState = true;
        return;
    }

    // Configure difficulty
    if (pendingGameType == GameType::SIGNAL_ECHO) {
        auto* echo = static_cast<SignalEcho*>(game);
        if (echo) {
            SignalEchoConfig config = hardMode ? SIGNAL_ECHO_HARD : SIGNAL_ECHO_EASY;
            config.managedMode = true;
            echo->getConfig() = config;
        }
    } else if (pendingGameType == GameType::FIREWALL_DECRYPT) {
        auto* fw = static_cast<FirewallDecrypt*>(game);
        if (fw) {
            FirewallDecryptConfig config = hardMode ? FIREWALL_DECRYPT_HARD : FIREWALL_DECRYPT_EASY;
            config.managedMode = true;
            fw->getConfig() = config;
        }
    } else if (pendingGameType == GameType::GHOST_RUNNER) {
        auto* gr = static_cast<GhostRunner*>(game);
        if (gr) {
            GhostRunnerConfig config = hardMode ? GHOST_RUNNER_HARD : GHOST_RUNNER_EASY;
            config.managedMode = true;
            gr->getConfig() = config;
        }
    } else if (pendingGameType == GameType::SPIKE_VECTOR) {
        auto* sv = static_cast<SpikeVector*>(game);
        if (sv) {
            SpikeVectorConfig config = hardMode ? SPIKE_VECTOR_HARD : SPIKE_VECTOR_EASY;
            config.managedMode = true;
            sv->getConfig() = config;
        }
    } else if (pendingGameType == GameType::CIPHER_PATH) {
        auto* cp = static_cast<CipherPath*>(game);
        if (cp) {
            CipherPathConfig config = hardMode ? CIPHER_PATH_HARD : CIPHER_PATH_EASY;
            config.managedMode = true;
            cp->getConfig() = config;
        }
    } else if (pendingGameType == GameType::EXPLOIT_SEQUENCER) {
        auto* es = static_cast<ExploitSequencer*>(game);
        if (es) {
            ExploitSequencerConfig config = hardMode ? EXPLOIT_SEQUENCER_HARD : EXPLOIT_SEQUENCER_EASY;
            config.managedMode = true;
            es->getConfig() = config;
        }
    } else if (pendingGameType == GameType::BREACH_DEFENSE) {
        auto* bd = static_cast<BreachDefense*>(game);
        if (bd) {
            BreachDefenseConfig config = hardMode ? BREACH_DEFENSE_HARD : BREACH_DEFENSE_EASY;
            config.managedMode = true;
            bd->getConfig() = config;
        }
    }

    game->resetGame();
    gameLaunched = true;
    PDN->setActiveApp(StateId(appId));
}
