#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"

/*
 * ConnectionSuccessful - Brief celebration state after successful handshake
 */
class ConnectionSuccessful : public State {
public:
    explicit ConnectionSuccessful(Player *player);
    ~ConnectionSuccessful();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToCountdown();

private:
    Player *player;
    bool lightsOn = true;
    int flashDelay = 400;
    uint8_t transitionThreshold = 12;
    uint8_t alertCount = 0;
    SimpleTimer flashTimer;
};
