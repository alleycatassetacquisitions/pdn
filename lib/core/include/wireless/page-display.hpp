#pragma once

#include "device/drivers/button.hpp"
#include "device/drivers/display.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/page-cmd.hpp"
#include "wireless/page-manager.hpp"

/** Shared OLED page presentation used by FDN and PDN idle-safe page states. */
class PageDisplayController {
public:
    void begin(Display* display, Button* primaryButton, PageManager* pageManager) {
        this->display = display;
        this->primaryButton = primaryButton;
        this->pageManager = pageManager;
        dismissRequested = false;
        finished = false;

        if (pageManager == nullptr || !pageManager->hasPendingPage()) {
            finished = true;
            return;
        }

        page = pageManager->pendingPage();
        pageManager->clearPendingPage();

        const unsigned long ms =
            static_cast<unsigned long>(page.durationSeconds) * 1000UL;
        durationTimer.setTimer(ms > 0 ? ms : 1);

        if (display != nullptr) {
            display->invalidateScreen()
                ->setGlyphMode(FontMode::TEXT)
                ->drawText(page.text.c_str(), 0, 32);
            display->render();
        }

        if (primaryButton != nullptr) {
            primaryButton->setButtonPress(
                [](void* ctx) {
                    static_cast<PageDisplayController*>(ctx)->dismissRequested = true;
                },
                this, ButtonInteraction::CLICK);
        }
    }

    void loop() {
        if (finished) {
            return;
        }
        durationTimer.updateTime();
        if (dismissRequested || durationTimer.expired()) {
            finished = true;
        }
    }

    void end() {
        if (primaryButton != nullptr) {
            primaryButton->removeButtonCallbacks();
        }
        durationTimer.invalidate();
        dismissRequested = false;
    }

    bool isFinished() const { return finished; }

    const PageCmd& currentPage() const { return page; }

private:
    Display* display = nullptr;
    Button* primaryButton = nullptr;
    PageManager* pageManager = nullptr;
    PageCmd page;
    SimpleTimer durationTimer;
    bool dismissRequested = false;
    bool finished = false;
};
