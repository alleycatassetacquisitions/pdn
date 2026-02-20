#include "apps/speeder/speeder-app-states.hpp"
#include "device/device.hpp"
#include "apps/speeder/speeder-app-resources.hpp"
#include "device/drivers/logger.hpp"
#include <random>

static const char* TAG = "Speeding";

Speeding::Speeding() : State(SPEEDING) {}

void Speeding::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted - initializing display and buttons");
    
    PDN->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::ARCADE_1)
    ->renderGlyph(speeder, 2, 42)
    ->render();

    PDN->getPrimaryButton()->setButtonPress([](void *ctx) {
        Speeding* speedingState = static_cast<Speeding*>(ctx);
        speedingState->moveLaneUp();
        
    }, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress([](void *ctx) {
        Speeding* speedingState = static_cast<Speeding*>(ctx);
        speedingState->moveLaneDown();
    }, this, ButtonInteraction::CLICK);
    
    currentLane = 1;
    collisionOccurred = false;

    LOG_I(TAG, "State mounted - initialization complete");

    barrierController.start();
};

void Speeding::onStateLoop(Device *PDN) {
    LOG_D(TAG, "Loop iteration started");
    
    PDN->getDisplay()->invalidateScreen();

    renderPlayerLocation(PDN);
    renderBarriers(PDN);
    renderExplosion(PDN);

    PDN->getDisplay()->render();

    checkCollision();
    
    LOG_D(TAG, "Loop iteration complete");
}

void Speeding::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounting - cleaning up");
    
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    
    LOG_I(TAG, "State dismounted");
};

void Speeding::renderPlayerLocation(Device *PDN) {
    LOG_D(TAG, "Rendering player in lane: %d", currentLane);
    
    switch(currentLane) {
        case 0:
            PDN->getDisplay()->renderGlyph(speeder, 2, 20);
            break;
        case 1:
            PDN->getDisplay()->renderGlyph(speeder, 2, 42);
            break;
        case 2:
            PDN->getDisplay()->renderGlyph(speeder, 2, 63);
            break;
    }
}

void Speeding::moveLaneUp() {
    LOG_D(TAG, "MoveLaneUp called - current lane: %d", currentLane);
    
    if(currentLane == 0) {
        LOG_D(TAG, "Already at top lane, cannot move up");
        return;
    }
    currentLane--;
    
    LOG_I(TAG, "Moved up to lane: %d", currentLane);
}

void Speeding::moveLaneDown() {
    LOG_D(TAG, "MoveLaneDown called - current lane: %d", currentLane);
    
    if(currentLane == 2) {
        LOG_D(TAG, "Already at bottom lane, cannot move down");
        return;
    }
    currentLane++;
    
    LOG_I(TAG, "Moved down to lane: %d", currentLane);
}

void Speeding::renderScore(Device *PDN) {}

void Speeding::renderBarriers(Device *PDN) {
    const std::vector<Barrier>& barriers = barrierController.updateBarriers();
    for(const auto& barrier : barriers) {
        PDN->getDisplay()->renderGlyph(barrier.glyph, barrier.xCoord, barrier.lane * 21 + 20);
    }
}

void Speeding::checkCollision() {
    if(collisionOccurred) return;

    for(const auto& barrier : barrierController.getBarriers()) {
        if(barrier.lane == currentLane && barrier.xCoord < 16) {
            LOG_I(TAG, "Collision detected in lane %d", currentLane);
            collisionOccurred = true;
            barrierController.freeze();
            collisionTimer.setTimer(1000);
            return;
        }
    }
}

void Speeding::renderExplosion(Device *PDN) {
    if(!collisionOccurred) return;

    float progress = (float)collisionTimer.getElapsedTime() / 1000.0f;
    int radius = (int)(progress * maxExplosionRadius);
    if(radius < 1) return;

    int centerX = 6;
    int centerY = currentLane * 21 + 14;

    PDN->getDisplay()->drawShape(Circle(centerX, centerY, radius, true));
}

void Speeding::renderPowerups(Device *PDN) {}