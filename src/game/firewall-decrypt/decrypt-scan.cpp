#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/ui-clean-minimal.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>
#include <algorithm>

static const char* TAG = "DecryptScan";

DecryptScan::DecryptScan(FirewallDecrypt* game) : State(DECRYPT_SCAN) {
    this->game = game;
}

DecryptScan::~DecryptScan() {
    game = nullptr;
}

void DecryptScan::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    displayIsDirty = false;
    timedOut = false;

    auto& session = game->getSession();

    LOG_I(TAG, "Scan round %d, target: %s, %zu candidates",
          session.currentRound + 1,
          session.target.c_str(),
          session.candidates.size());

    // Initialize cursor at position 1 (first candidate after banner)
    cursorIndex = session.cursorIndex;

    // Set up button callbacks
    parameterizedCallbackFunction scrollCb = [](void* ctx) {
        auto* self = static_cast<DecryptScan*>(ctx);
        auto& session = self->game->getSession();
        int listSize = static_cast<int>(session.candidates.size());

        // UP button scrolls forward
        self->cursorIndex++;

        // Wrap behavior: skip index 0 (banner), reset to 1
        if (self->cursorIndex >= listSize) {
            self->cursorIndex = 1;
        }

        session.cursorIndex = self->cursorIndex;
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
        // Calculate depleting timer for hard mode
        int timerForRound = game->getConfig().timeLimitMs;

        // Hard mode: timer depletes each round (12s, 10s, 8s, 6s)
        if (game->getConfig().numRounds == 4 && game->getConfig().numCandidates >= 40) {
            timerForRound = 12000 - (session.currentRound * 2000);
            if (timerForRound < 6000) timerForRound = 6000;
        }

        roundTimer.setTimer(timerForRound);
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

        // Redraw every frame to update timer bar
        renderUi(PDN);

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
    auto& config = game->getConfig();
    auto display = PDN->getDisplay();

    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // 1. HUD bar (inverted header, y0-9)
    display->drawBox(0, 0, 128, 10);
    display->setDrawColor(0);
    display->drawText("DECRYPT", 2, 9);

    // Round counter
    char roundText[8];
    snprintf(roundText, sizeof(roundText), "%d/%d",
             session.currentRound + 1, config.numRounds);
    display->drawText(roundText, 104, 9);
    display->setDrawColor(1);

    // 2. List area (y10-49, 5 visible items × 8px each)
    int listSize = static_cast<int>(session.candidates.size());
    int visibleCount = 5;

    // Calculate viewport (5 items centered on cursor)
    int viewStart = cursorIndex - 2;
    if (viewStart < 0) viewStart = 0;
    if (viewStart + visibleCount > listSize) {
        viewStart = listSize - visibleCount;
        if (viewStart < 0) viewStart = 0;
    }

    for (int i = 0; i < visibleCount && (viewStart + i) < listSize; i++) {
        int listIndex = viewStart + i;
        int y = 10 + (i * 8);
        const char* addr = session.candidates[listIndex].c_str();

        if (listIndex == cursorIndex) {
            // Cursor: inverted highlight
            display->drawBox(2, y, 120, 8);
            display->setDrawColor(0);
            display->drawText("> ", 4, y + 7);
            display->drawText(addr, 16, y + 7);
            display->setDrawColor(1);
        } else if (listIndex == 0) {
            // Banner (target MAC): framed
            display->drawFrame(2, y, 80, 8);
            display->drawText(addr, 6, y + 7);
        } else {
            // Normal item
            display->drawText("  ", 4, y + 7);
            display->drawText(addr, 16, y + 7);
        }
    }

    // 3. Scrollbar (x123-127, y10-49)
    int trackTop = 10;
    int trackHeight = 40;
    display->drawFrame(123, trackTop, 5, trackHeight);

    // Scrollbar thumb
    int thumbHeight = std::max(3, (visibleCount * trackHeight) / listSize);
    int thumbOffset = ((cursorIndex - 1) * (trackHeight - thumbHeight)) / std::max(1, listSize - 2);
    if (thumbOffset < 0) thumbOffset = 0;
    if (thumbOffset + thumbHeight > trackHeight) thumbOffset = trackHeight - thumbHeight;
    display->drawBox(124, trackTop + thumbOffset, 3, thumbHeight);

    // 4. Divider (y50)
    display->drawBox(0, 50, 128, 1);

    // 5. Fuse bar (y51-57, always visible)
    if (config.timeLimitMs > 0) {
        int timerY = 51;
        int barWidth = 100;
        int barX = 4;

        // Frame
        display->drawFrame(barX, timerY, barWidth + 2, 7);

        // Fill (depletes as time runs out)
        roundTimer.updateTime();
        unsigned long elapsed = roundTimer.getElapsedTime();

        // Calculate timer duration based on round
        int timerForRound = config.timeLimitMs;
        if (config.numRounds == 4 && config.numCandidates >= 40) {
            timerForRound = 12000 - (session.currentRound * 2000);
            if (timerForRound < 6000) timerForRound = 6000;
        }

        unsigned long remaining = (elapsed < (unsigned long)timerForRound)
            ? ((unsigned long)timerForRound - elapsed) : 0;
        float timeRatio = (float)remaining / (float)timerForRound;
        int fillWidth = (int)(timeRatio * barWidth);
        if (fillWidth > 0) {
            display->drawBox(barX + 1, timerY + 1, fillWidth, 5);
        }

        // Time remaining text
        char timeText[8];
        snprintf(timeText, sizeof(timeText), "%d.%ds",
                 (int)(remaining / 1000), (int)((remaining % 1000) / 100));
        display->drawText(timeText, 108, timerY + 6);
    } else {
        // No timer: show lives pips instead
        int pipY = 53;
        for (int i = 0; i < session.mistakesRemaining; i++) {
            display->drawBox(4 + (i * 10), pipY, 6, 4);
        }
    }

    // 6. Controls bar (y58-63)
    display->drawText("[UP]SCR", 2, 63);
    display->drawText("SEL[DN]", 90, 63);

    display->render();
}
