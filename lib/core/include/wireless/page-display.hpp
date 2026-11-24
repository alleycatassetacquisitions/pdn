#pragma once

#include "device/device.hpp"
#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/page-cmd.hpp"
#include "wireless/page-manager.hpp"

/**
 * Shows the device's pending Mission Control page until its duration runs out or
 * the primary button is clicked. Shared by FDN Idle and PDN Quickdraw; the owning
 * app adds the transition back with isFinished().
 */
class PageDisplayState : public TypedState<Device> {
public:
    explicit PageDisplayState(int stateId) : TypedState<Device>(stateId) {}

    void onStateMounted(Device* device) override {
        dismissRequested = false;
        finished = false;

        PageCmd page;
        PageManager* pages = device->getPageManager();
        if (pages == nullptr || !pages->takePendingPage(page)) {
            finished = true;
            return;
        }

        const unsigned long ms = static_cast<unsigned long>(page.durationSeconds) * 1000UL;
        durationTimer.setTimer(ms > 0 ? ms : 1);

        device->getDisplay()
            ->invalidateScreen()
            ->setGlyphMode(FontMode::TEXT)
            ->drawText(page.text.c_str(), 0, 32);
        device->getDisplay()->render();

        device->getPrimaryButton()->setButtonPress(
            [](void* ctx) { static_cast<PageDisplayState*>(ctx)->dismissRequested = true; },
            this, ButtonInteraction::CLICK);
    }

    void onStateLoop(Device* device) override {
        (void)device;
        if (!finished && (dismissRequested || durationTimer.expired())) {
            finished = true;
        }
    }

    void onStateDismounted(Device* device) override {
        device->getPrimaryButton()->removeButtonCallbacks();
        durationTimer.invalidate();
        dismissRequested = false;
    }

    bool isFinished() const { return finished; }

private:
    SimpleTimer durationTimer;
    bool dismissRequested = false;
    bool finished = false;
};
