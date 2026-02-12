#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>

static const char* TAG = "DecryptScan";

DecryptScan::DecryptScan(FirewallDecrypt* game) : State(DECRYPT_SCAN) {
    this->game = game;
}

DecryptScan::~DecryptScan() {
    game = nullptr;
}

void DecryptScan::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    cursorIndex = 0;
    displayIsDirty = false;
    timedOut = false;

    LOG_I(TAG, "Scan round %d, target: %s, %zu candidates",
          game->getSession().currentRound + 1,
          game->getSession().target.c_str(),
          game->getSession().candidates.size());

    // Set up button callbacks
    parameterizedCallbackFunction scrollCb = [](void* ctx) {
        auto* self = static_cast<DecryptScan*>(ctx);
        int listSize = static_cast<int>(self->game->getSession().candidates.size());
        self->cursorIndex = (self->cursorIndex + 1) % listSize;
        self->displayIsDirty = true;
    };

    parameterizedCallbackFunction confirmCb = [](void* ctx) {
        auto* self = static_cast<DecryptScan*>(ctx);
        self->transitionToEvaluateState = true;
    };

    PDN->getPrimaryButton()->setButtonPress(scrollCb, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(confirmCb, this, ButtonInteraction::CLICK);

    // Start time limit if configured
    if (game->getConfig().timeLimitMs > 0) {
        roundTimer.setTimer(game->getConfig().timeLimitMs);
    }

    renderUi(PDN);
}

void DecryptScan::onStateLoop(Device* PDN) {
    if (displayIsDirty) {
        renderUi(PDN);
        displayIsDirty = false;
    }

    if (game->getConfig().timeLimitMs > 0) {
        roundTimer.updateTime();
        if (roundTimer.expired()) {
            LOG_I(TAG, "Round timed out");
            timedOut = true;
            transitionToEvaluateState = true;
        }
    }
}

void DecryptScan::onStateDismounted(Device* PDN) {
    roundTimer.invalidate();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();

    // Store selection in session for evaluate to read
    game->getSession().selectedIndex = cursorIndex;
    game->getSession().timedOut = timedOut;
}

bool DecryptScan::transitionToEvaluate() {
    return transitionToEvaluateState;
}

void DecryptScan::renderUi(Device* PDN) {
    if (!PDN) return;

    auto& session = game->getSession();

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    // Header: target address
    char header[20];
    snprintf(header, sizeof(header), "FIND: %s", session.target.c_str());
    PDN->getDisplay()->drawText(header, 2, 8);

    // Show round counter
    char roundStr[16];
    snprintf(roundStr, sizeof(roundStr), "Round %d/%d",
             session.currentRound + 1, game->getConfig().numRounds);
    PDN->getDisplay()->drawText(roundStr, 80, 8);

    // Scrollable list: show up to 3 items centered on cursor
    int listSize = static_cast<int>(session.candidates.size());
    int visibleCount = (listSize < 3) ? listSize : 3;

    for (int i = 0; i < visibleCount; i++) {
        int idx = (cursorIndex + i) % listSize;
        int y = 24 + (i * 12);
        const char* addr = session.candidates[idx].c_str();

        if (idx == cursorIndex) {
            char line[20];
            snprintf(line, sizeof(line), "> %s", addr);
            PDN->getDisplay()->drawText(line, 2, y);
        } else {
            char line[20];
            snprintf(line, sizeof(line), "  %s", addr);
            PDN->getDisplay()->drawText(line, 2, y);
        }
    }

    PDN->getDisplay()->drawText("UP:scroll DOWN:ok", 5, 60);
    PDN->getDisplay()->render();
}
