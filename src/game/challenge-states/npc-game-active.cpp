#include "game/challenge-states.hpp"
#include "game/challenge-game.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NpcGameActive";

NpcGameActive::NpcGameActive(ChallengeGame* game) :
    State(ChallengeStateId::NPC_GAME_ACTIVE),
    game(game)
{
}

NpcGameActive::~NpcGameActive() {
    game = nullptr;
}

void NpcGameActive::onStateMounted(Device* PDN) {
    LOG_I(TAG, "NPC Game Active mounted");

    transitionToReceiveResultState = false;
    transitionToIdleState = false;
    currentRound = 0;
    totalRounds = 3;
    sequenceLength = 4;
    inputIndex = 0;
    playerLives = 3;
    playerScore = 0;
    gameComplete = false;
    playerWon = false;

    inactivityTimer.setTimer(INACTIVITY_TIMEOUT_MS);

    // Generate first sequence and send it
    generateSequence();
    sendSequence(PDN);

    renderDisplay(PDN);
}

void NpcGameActive::onStateLoop(Device* PDN) {
    inactivityTimer.updateTime();
    if (inactivityTimer.expired()) {
        LOG_W(TAG, "Inactivity timeout — returning to idle");
        transitionToIdleState = true;
    }
}

void NpcGameActive::onStateDismounted(Device* PDN) {
    inactivityTimer.invalidate();
    currentSequence.clear();
}

bool NpcGameActive::transitionToReceiveResult() {
    return transitionToReceiveResultState;
}

bool NpcGameActive::transitionToIdle() {
    return transitionToIdleState;
}

void NpcGameActive::generateSequence() {
    currentSequence.clear();
    for (int i = 0; i < sequenceLength; i++) {
        currentSequence.push_back(rand() % 2 == 0);
    }
}

void NpcGameActive::sendSequence(Device* PDN) {
    // Format: "gseq:<round>:<length>:<bits>"
    std::string bits;
    for (bool b : currentSequence) {
        bits += (b ? "1" : "0");
    }
    std::string msg = "gseq:" + std::to_string(currentRound) + ":" +
                      std::to_string(sequenceLength) + ":" + bits;

    // Send via ESP-NOW to the player
    // For now, send via serial as well (ESP-NOW requires peer MAC)
    PDN->writeString(msg.c_str());

    LOG_I(TAG, "Sent sequence: %s", msg.c_str());
    inputIndex = 0;
}

void NpcGameActive::onEspNowReceived(const std::string& message, Device* PDN) {
    // Handle "ginp:<round>:<index>:<value>"
    if (message.rfind("ginp:", 0) == 0) {
        inactivityTimer.setTimer(INACTIVITY_TIMEOUT_MS);  // Reset timeout

        // Parse: ginp:<round>:<index>:<value>
        size_t c1 = 4;  // colon in "ginp:"
        size_t c2 = message.find(':', c1 + 1);
        size_t c3 = message.find(':', c2 + 1);
        if (c2 == std::string::npos || c3 == std::string::npos) return;

        int inpIndex = std::stoi(message.substr(c2 + 1, c3 - c2 - 1));
        int inpValue = std::stoi(message.substr(c3 + 1));

        bool expected = currentSequence[inpIndex];
        bool playerInput = (inpValue == 1);
        bool correct = (expected == playerInput);

        if (!correct) {
            playerLives--;
        } else {
            playerScore += 100;
        }

        // Send validation: "gval:<correct>:<livesLeft>"
        std::string valMsg = "gval:" + std::string(correct ? "1" : "0") + ":" +
                             std::to_string(playerLives);
        PDN->writeString(valMsg.c_str());

        inputIndex++;

        // Check if lives exhausted
        if (playerLives <= 0) {
            gameComplete = true;
            playerWon = false;
            std::string resMsg = "gres:0:" + std::to_string(playerScore);
            PDN->writeString(resMsg.c_str());
            game->setLastResult(false);
            game->setLastScore(playerScore);
            transitionToReceiveResultState = true;
            return;
        }

        // Check if round complete
        if (inputIndex >= sequenceLength) {
            currentRound++;
            // Send round result: "grnd:<passed>:<nextRound>"
            std::string rndMsg = "grnd:1:" + std::to_string(currentRound);
            PDN->writeString(rndMsg.c_str());

            // Check if all rounds complete
            if (currentRound >= totalRounds) {
                gameComplete = true;
                playerWon = true;
                std::string resMsg = "gres:1:" + std::to_string(playerScore);
                PDN->writeString(resMsg.c_str());
                game->setLastResult(true);
                game->setLastScore(playerScore);
                transitionToReceiveResultState = true;
            } else {
                // Generate next round
                generateSequence();
                sendSequence(PDN);
            }
        }

        renderDisplay(PDN);
    }
}

void NpcGameActive::renderDisplay(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("GAME IN PROGRESS", 5, 15);

    std::string roundStr = "Round " + std::to_string(currentRound + 1) +
                           "/" + std::to_string(totalRounds);
    PDN->getDisplay()->drawText(roundStr.c_str(), 10, 35);

    std::string livesStr = "Lives: " + std::to_string(playerLives);
    PDN->getDisplay()->drawText(livesStr.c_str(), 10, 50);

    PDN->getDisplay()->render();
}
