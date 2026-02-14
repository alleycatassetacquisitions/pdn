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
#include "wireless/mac-functions.hpp"
#include "device/device-constants.hpp"
#include "device/device-types.hpp"
#include <cstdint>

static const char* TAG = "FdnDetected";

FdnDetected::FdnDetected(Player* player) :
    State(FDN_DETECTED),
    player(player)
{
}

FdnDetected::~FdnDetected() {
    player = nullptr;
}

void FdnDetected::onStateMounted(Device* PDN) {
    transitionToIdleState = false;
    transitionToFdnCompleteState = false;
    transitionToReencounterState = false;
    transitionToKonamiPuzzleState = false;
    fackReceived = false;
    macSent = false;
    handshakeComplete = false;

    // Read the pending FDN message from Player (set by Idle)
    fdnMessage = player->getPendingCdevMessage();

    LOG_I(TAG, "FDN detected, message: %s", fdnMessage.c_str());

    // Parse the FDN message
    if (!parseFdnMessage(fdnMessage, pendingGameType, pendingReward)) {
        LOG_W(TAG, "Failed to parse FDN message: %s", fdnMessage.c_str());
        transitionToIdleState = true;
        return;
    }

    LOG_I(TAG, "Challenge: %s, reward: %s",
           getGameDisplayName(pendingGameType),
           getKonamiButtonName(pendingReward));

    // Set up serial callback to listen for fack
    PDN->setOnStringReceivedCallback([this](const std::string& message) {
        LOG_I(TAG, "Serial received: %s", message.c_str());
        if (message == FDN_ACK) {
            LOG_I(TAG, "Received fack from NPC");
            fackReceived = true;
        }
    });

    // Send our MAC address to the NPC
    uint8_t* macAddr = PDN->getWirelessManager()->getMacAddress();
    const char* macStr = MacToString(macAddr);
    std::string macMessage = SEND_MAC_ADDRESS + std::string(macStr);
    PDN->writeString(macMessage.c_str());
    macSent = true;

    LOG_I(TAG, "Sent MAC: %s", macStr);

    // Start timeout
    timeoutTimer.setTimer(TIMEOUT_MS);

    // Display challenge info
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("CHALLENGE", 20, 15);
    PDN->getDisplay()->drawText(getGameDisplayName(pendingGameType), 20, 35);
    PDN->getDisplay()->drawText("CONNECTING...", 10, 55);
    PDN->getDisplay()->render();
}

void FdnDetected::onStateLoop(Device* PDN) {
    if (fackReceived && !handshakeComplete) {
        handshakeComplete = true;

        // Store game info on Player for FdnReencounter / FdnComplete to read
        player->setLastFdnGameType(static_cast<int>(pendingGameType));
        player->setLastFdnReward(static_cast<uint8_t>(pendingReward));

        // Check if player has all 7 Konami buttons -- route to Konami Puzzle
        if (player->hasAllKonamiButtons()) {
            LOG_I(TAG, "All 7 buttons collected - routing to Konami Puzzle!");
            transitionToKonamiPuzzleState = true;
            return;
        }

        // Check per-game progression to decide routing
        bool hasButton = player->hasUnlockedButton(static_cast<uint8_t>(pendingReward));
        bool hasProfile = player->hasColorProfileEligibility(static_cast<int>(pendingGameType));

        if (hasButton) {
            // Re-encounter -- route to FdnReencounter for difficulty choice
            LOG_I(TAG, "Re-encounter: hasButton=%d hasProfile=%d", hasButton, hasProfile);
            transitionToReencounterState = true;
            return;
        }

        // First encounter -- launch EASY directly
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

        // First encounter is always EASY
        if (pendingGameType == GameType::SIGNAL_ECHO) {
            auto* echo = static_cast<SignalEcho*>(game);
            if (echo) {
                SignalEchoConfig config = SIGNAL_ECHO_EASY;
                config.managedMode = true;
                echo->getConfig() = config;
            }
        } else if (pendingGameType == GameType::FIREWALL_DECRYPT) {
            auto* fw = static_cast<FirewallDecrypt*>(game);
            if (fw) {
                FirewallDecryptConfig config = FIREWALL_DECRYPT_EASY;
                config.managedMode = true;
                fw->getConfig() = config;
            }
        } else if (pendingGameType == GameType::GHOST_RUNNER) {
            auto* gr = static_cast<GhostRunner*>(game);
            if (gr) {
                GhostRunnerConfig config = GHOST_RUNNER_EASY;
                config.managedMode = true;
                gr->getConfig() = config;
            }
        } else if (pendingGameType == GameType::SPIKE_VECTOR) {
            auto* sv = static_cast<SpikeVector*>(game);
            if (sv) {
                SpikeVectorConfig config = SPIKE_VECTOR_EASY;
                config.managedMode = true;
                sv->getConfig() = config;
            }
        } else if (pendingGameType == GameType::CIPHER_PATH) {
            auto* cp = static_cast<CipherPath*>(game);
            if (cp) {
                CipherPathConfig config = CIPHER_PATH_EASY;
                config.managedMode = true;
                cp->getConfig() = config;
            }
        } else if (pendingGameType == GameType::EXPLOIT_SEQUENCER) {
            auto* es = static_cast<ExploitSequencer*>(game);
            if (es) {
                ExploitSequencerConfig config = EXPLOIT_SEQUENCER_EASY;
                config.managedMode = true;
                es->getConfig() = config;
            }
        } else if (pendingGameType == GameType::BREACH_DEFENSE) {
            auto* bd = static_cast<BreachDefense*>(game);
            if (bd) {
                BreachDefenseConfig config = BREACH_DEFENSE_EASY;
                config.managedMode = true;
                bd->getConfig() = config;
            }
        }

        LOG_I(TAG, "First encounter: launching %s (EASY)", getGameDisplayName(pendingGameType));

        // Increment easy attempt counter
        player->incrementEasyAttempts(pendingGameType);

        player->setRecreationalMode(false);
        game->resetGame();
        PDN->setActiveApp(StateId(appId));
        return;
    }

    if (timeoutTimer.expired()) {
        LOG_W(TAG, "FDN handshake timed out");
        transitionToIdleState = true;
    }
}

void FdnDetected::onStateDismounted(Device* PDN) {
    timeoutTimer.invalidate();
    fdnMessage.clear();
    fackReceived = false;
    macSent = false;
    player->clearPendingChallenge();
    PDN->clearCallbacks();
}

std::unique_ptr<Snapshot> FdnDetected::onStatePaused(Device* PDN) {
    auto snapshot = std::make_unique<FdnDetectedSnapshot>();
    snapshot->gameType = pendingGameType;
    snapshot->reward = pendingReward;
    snapshot->handshakeComplete = handshakeComplete;
    return snapshot;
}

void FdnDetected::onStateResumed(Device* PDN, Snapshot* snapshot) {
    if (snapshot) {
        auto* fdnSnapshot = static_cast<FdnDetectedSnapshot*>(snapshot);
        pendingGameType = fdnSnapshot->gameType;
        pendingReward = fdnSnapshot->reward;
        handshakeComplete = fdnSnapshot->handshakeComplete;
    }

    // If handshake was complete when we paused, the minigame is done
    if (handshakeComplete) {
        transitionToFdnCompleteState = true;
    }
}

bool FdnDetected::transitionToIdle() {
    return transitionToIdleState;
}

bool FdnDetected::transitionToFdnComplete() {
    return transitionToFdnCompleteState;
}

bool FdnDetected::transitionToReencounter() {
    return transitionToReencounterState;
}

bool FdnDetected::transitionToKonamiPuzzle() {
    return transitionToKonamiPuzzleState;
}
