#pragma once

#include "state/state.hpp"
#include "state/instructions-state.hpp"
#include "utils/simple-timer.hpp"
#include "apps/speeder/barrier-controller.hpp"
#include "image.hpp"
#include "images-raw.hpp"

enum SpeederAppStateId {
    INSTRUCTIONS = 0,
    SPEEDING = 1,
    GAME_OVER = 2,
    TRY_AGAIN = 3,
};

class Speeding : public State {
public:
    explicit Speeding();
    ~Speeding() override = default;
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    void moveLaneUp();
    void moveLaneDown();

    void renderPlayerLocation(Device *PDN);
    void renderScore(Device *PDN);
    void renderBarriers(Device *PDN);
    void renderPowerups(Device *PDN);
    void renderExplosion(Device *PDN);
    void checkCollision();

    bool gameOver() const { return collisionOccurred && collisionTimer.expired(); }

private:
    int speed = 0;
    int currentLane = 1;
    int score = 0;

    bool collisionOccurred = false;
    mutable SimpleTimer collisionTimer;
    static constexpr int maxExplosionRadius = 130;

    BarrierController barrierController;
};

class GameOver : public State {
public:
    explicit GameOver();
    ~GameOver() override = default;
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool shouldTransitionToTryAgain() const;

private:
    Image loseImage = Image(image_loser, 128, 64, 32, 0);

    SimpleTimer gameOverTimer = SimpleTimer();
    bool transitionToTryAgainState = false;
    const int GAME_OVER_DURATION = 3000;
};