#include "game/konami-states/konami-handshake.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include <cstdint>

static const char* TAG = "KonamiHandshake";

KonamiHandshake::KonamiHandshake(Player* player) :
    State(0),  // State ID will be set by state machine
    player(player),
    fdnGameType(FdnGameType::SIGNAL_ECHO),
    gameTypeReceived(false),
    targetStateIndex(-1),
    transitionReady(false)
{
}

KonamiHandshake::~KonamiHandshake() {
    player = nullptr;
}

void KonamiHandshake::onStateMounted(Device* PDN) {
    gameTypeReceived = false;
    transitionReady = false;
    targetStateIndex = -1;

    LOG_I(TAG, "KonamiHandshake mounted - awaiting FDN game type");

    // Register callback to receive FDN game type message
    PDN->setOnStringReceivedCallback([this](const std::string& message) {
        handleSerialMessage(message);
    });

    // Display waiting state
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("KONAMI", 35, 20);
    PDN->getDisplay()->drawText("HANDSHAKE", 20, 35);
    PDN->getDisplay()->drawText("CONNECTING...", 10, 50);
    PDN->getDisplay()->render();
}

void KonamiHandshake::onStateLoop(Device* PDN) {
    // Once we receive the game type, calculate target and prepare transition
    if (gameTypeReceived && !transitionReady) {
        targetStateIndex = calculateTargetState(fdnGameType);

        if (targetStateIndex >= 0) {
            LOG_I(TAG, "Routing to state index: %d", targetStateIndex);
            transitionReady = true;
        } else {
            LOG_E(TAG, "Invalid target state calculated - cannot route");
        }
    }
}

void KonamiHandshake::onStateDismounted(Device* PDN) {
    PDN->clearCallbacks();
    gameTypeReceived = false;
    transitionReady = false;
    targetStateIndex = -1;
}

bool KonamiHandshake::shouldTransition() const {
    return transitionReady;
}

int KonamiHandshake::getTargetStateIndex() const {
    return targetStateIndex;
}

void KonamiHandshake::handleSerialMessage(const std::string& message) {
    // Expected format: "fgame:<gameType>" where gameType is 0-7
    if (message.rfind("fgame:", 0) != 0) {
        LOG_W(TAG, "Received non-fgame message: %s", message.c_str());
        return;
    }

    std::string gameTypeStr = message.substr(6);  // Skip "fgame:"

    try {
        int gameTypeInt = std::stoi(gameTypeStr);

        if (gameTypeInt < 0 || gameTypeInt > 7) {
            LOG_W(TAG, "Invalid game type value: %d", gameTypeInt);
            return;
        }

        fdnGameType = static_cast<FdnGameType>(gameTypeInt);
        gameTypeReceived = true;

        LOG_I(TAG, "Received FDN game type: %d", gameTypeInt);
    } catch (const std::exception& e) {
        LOG_W(TAG, "Failed to parse game type from: %s", message.c_str());
    }
}

int KonamiHandshake::calculateTargetState(FdnGameType gameType) {
    // Special case: KONAMI_CODE FDN
    if (gameType == FdnGameType::KONAMI_CODE) {
        if (player->hasAllKonamiButtons()) {
            LOG_I(TAG, "KONAMI_CODE FDN - all buttons collected → CodeEntry");
            return 29;  // CodeEntry state index
        } else {
            LOG_I(TAG, "KONAMI_CODE FDN - incomplete buttons → CodeRejected");
            return 30;  // CodeRejected state index
        }
    }

    // Regular game FDN - get the game index (0-6)
    int gameIndex = static_cast<int>(gameType);

    // Convert FdnGameType to KonamiButton to check progress
    // Map: SIGNAL_ECHO→UP, GHOST_RUNNER→START, SPIKE_VECTOR→DOWN,
    //      FIREWALL_DECRYPT→LEFT, CIPHER_PATH→RIGHT, EXPLOIT_SEQUENCER→B, BREACH_DEFENSE→A
    uint8_t buttonIndex;
    switch (gameType) {
        case FdnGameType::SIGNAL_ECHO:       buttonIndex = 0; break;  // UP
        case FdnGameType::GHOST_RUNNER:      buttonIndex = 6; break;  // START
        case FdnGameType::SPIKE_VECTOR:      buttonIndex = 1; break;  // DOWN
        case FdnGameType::FIREWALL_DECRYPT:  buttonIndex = 2; break;  // LEFT
        case FdnGameType::CIPHER_PATH:       buttonIndex = 3; break;  // RIGHT
        case FdnGameType::EXPLOIT_SEQUENCER: buttonIndex = 4; break;  // B
        case FdnGameType::BREACH_DEFENSE:    buttonIndex = 5; break;  // A
        default:
            LOG_E(TAG, "Unknown game type in calculateTargetState");
            return -1;
    }

    bool hasButton = player->hasUnlockedButton(buttonIndex);
    bool hasBoon = player->hasKonamiBoon();

    // Convert FdnGameType to GameType for eligibility check
    // The mapping is: SIGNAL_ECHO=0→SIGNAL_ECHO=7, GHOST_RUNNER=1→GHOST_RUNNER=1, etc.
    // Actually looking at device-types.hpp:
    // GameType: QUICKDRAW=0, GHOST_RUNNER=1, SPIKE_VECTOR=2, FIREWALL_DECRYPT=3,
    //           CIPHER_PATH=4, EXPLOIT_SEQUENCER=5, BREACH_DEFENSE=6, SIGNAL_ECHO=7
    int gameTypeValue;
    switch (gameType) {
        case FdnGameType::SIGNAL_ECHO:       gameTypeValue = 7; break;
        case FdnGameType::GHOST_RUNNER:      gameTypeValue = 1; break;
        case FdnGameType::SPIKE_VECTOR:      gameTypeValue = 2; break;
        case FdnGameType::FIREWALL_DECRYPT:  gameTypeValue = 3; break;
        case FdnGameType::CIPHER_PATH:       gameTypeValue = 4; break;
        case FdnGameType::EXPLOIT_SEQUENCER: gameTypeValue = 5; break;
        case FdnGameType::BREACH_DEFENSE:    gameTypeValue = 6; break;
        default:
            LOG_E(TAG, "Unknown game type for eligibility check");
            return -1;
    }

    bool hardModeUnlocked = player->hasColorProfileEligibility(gameTypeValue);

    // Decision tree routing:
    // 1. If hasBoon → MasteryReplay (index 22 + gameType)
    // 2. If hardModeUnlocked && !hasBoon → HardLaunch (index 15 + gameType)
    // 3. If hasButton → ReplayEasy (index 8 + gameType)
    // 4. Else → EasyLaunch (index 1 + gameType)

    if (hasBoon) {
        int targetIndex = 22 + gameIndex;
        LOG_I(TAG, "hasBoon → MasteryReplay, index=%d", targetIndex);
        return targetIndex;
    }

    if (hardModeUnlocked && !hasBoon) {
        int targetIndex = 15 + gameIndex;
        LOG_I(TAG, "hardModeUnlocked && !hasBoon → HardLaunch, index=%d", targetIndex);
        return targetIndex;
    }

    if (hasButton) {
        int targetIndex = 8 + gameIndex;
        LOG_I(TAG, "hasButton → ReplayEasy, index=%d", targetIndex);
        return targetIndex;
    }

    // First encounter
    int targetIndex = 1 + gameIndex;
    LOG_I(TAG, "First encounter → EasyLaunch, index=%d", targetIndex);
    return targetIndex;
}
