#pragma once

#include "state/state.hpp"
#include "device/device.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "utils/simple-timer.hpp"

class UploadPendingHacksState : public State {
public:
    explicit UploadPendingHacksState(HackedPlayersManager* hackedPlayersManager);
    ~UploadPendingHacksState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToIdle();

private:
    HackedPlayersManager* hackedPlayersManager;

    SimpleTimer glyphTimer;
    SimpleTimer fallbackTimer;
    bool contentReady = false;
    int pendingCount = 0;
    int completedCount = 0;

    static constexpr int FALLBACK_TIMEOUT_MS = 15000;
};
