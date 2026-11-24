#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "device/device.hpp"
#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/page-cmd.hpp"
#include "wireless/page-manager.hpp"

/**
 * Word-wraps page text to lines no wider than `maxWidth` pixels, as measured by
 * `textWidth`. Honors '\n'; a word wider than a line is split by characters.
 * Always returns at least one line.
 */
inline std::vector<std::string> wrapPageText(const std::string& text, int maxWidth,
                                             const std::function<int(const char*)>& textWidth) {
    std::vector<std::string> lines;
    auto fits = [&](const std::string& s) { return textWidth(s.c_str()) <= maxWidth; };

    size_t paraStart = 0;
    while (paraStart <= text.size()) {
        size_t paraEnd = text.find('\n', paraStart);
        if (paraEnd == std::string::npos) {
            paraEnd = text.size();
        }
        const std::string para = text.substr(paraStart, paraEnd - paraStart);

        std::string line;
        size_t pos = 0;
        while (pos < para.size()) {
            if (para[pos] == ' ') {
                ++pos;
                continue;
            }
            size_t wordEnd = para.find(' ', pos);
            if (wordEnd == std::string::npos) {
                wordEnd = para.size();
            }
            std::string word = para.substr(pos, wordEnd - pos);
            pos = wordEnd;

            const std::string joined = line.empty() ? word : line + " " + word;
            if (fits(joined)) {
                line = joined;
                continue;
            }
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            // Split a word wider than the screen.
            while (!fits(word)) {
                size_t take = 1;
                while (take < word.size() && fits(word.substr(0, take + 1))) {
                    ++take;
                }
                lines.push_back(word.substr(0, take));
                word.erase(0, take);
            }
            line = word;
        }
        lines.push_back(line);
        paraStart = paraEnd + 1;
    }

    while (lines.size() > 1 && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

/**
 * Shows queued Mission Control pages. Long text is split into screens of
 * LINES_PER_SCREEN lines; each screen stays up for the page's duration or until
 * the primary button is clicked, which moves to the next screen. After the last
 * screen the state finishes; the owning app returns to Idle, which mounts this
 * state again while pages are still queued. Shared by FDN Idle and PDN Quickdraw.
 */
class PageDisplayState : public TypedState<Device> {
public:
    static constexpr size_t LINES_PER_SCREEN = 3;

    explicit PageDisplayState(int stateId) : TypedState<Device>(stateId) {}

    void onStateMounted(Device* device) override {
        advanceRequested = false;
        finished = false;
        screens.clear();
        screenIndex = 0;

        pages = device->getPageManager();
        PageCmd page;
        if (pages == nullptr || !pages->takePendingPage(page)) {
            finished = true;
            return;
        }

        Display* display = device->getDisplay();
        display->setGlyphMode(FontMode::TEXT);
        const std::vector<std::string> lines = wrapPageText(
            page.text, display->getWidth(),
            [display](const char* s) { return display->getTextWidth(s); });
        for (size_t i = 0; i < lines.size(); i += LINES_PER_SCREEN) {
            screens.emplace_back(lines.begin() + i,
                                 lines.begin() + std::min(lines.size(), i + LINES_PER_SCREEN));
        }

        const unsigned long ms = static_cast<unsigned long>(page.durationSeconds) * 1000UL;
        screenMs = ms > 0 ? ms : 1;

        device->getPrimaryButton()->setButtonPress(
            [](void* ctx) { static_cast<PageDisplayState*>(ctx)->advanceRequested = true; },
            this, ButtonInteraction::CLICK);

        showScreen(device);
    }

    void onStateLoop(Device* device) override {
        if (finished || !(advanceRequested || screenTimer.expired())) {
            return;
        }
        advanceRequested = false;
        if (++screenIndex >= screens.size()) {
            finished = true;
            return;
        }
        showScreen(device);
    }

    void onStateDismounted(Device* device) override {
        device->getPrimaryButton()->removeButtonCallbacks();
        screenTimer.invalidate();
        advanceRequested = false;
    }

    bool isFinished() const { return finished; }

private:
    static constexpr int LINE_Y[LINES_PER_SCREEN] = {14, 30, 46};
    static constexpr int FOOTER_Y = 62;

    /** Draws the current screen. Footer: "+N" more pages waiting, "i/n" screens. */
    void showScreen(Device* device) {
        Display* display = device->getDisplay();
        display->invalidateScreen()->setGlyphMode(FontMode::TEXT);
        const std::vector<std::string>& lines = screens[screenIndex];
        for (size_t i = 0; i < lines.size(); ++i) {
            display->drawText(lines[i].c_str(), 0, LINE_Y[i]);
        }

        const size_t waiting = pages->pendingCount();
        if (waiting > 0) {
            const std::string more = "+" + std::to_string(waiting);
            display->drawText(more.c_str(), 0, FOOTER_Y);
        }
        if (screens.size() > 1) {
            const std::string position =
                std::to_string(screenIndex + 1) + "/" + std::to_string(screens.size());
            display->drawText(position.c_str(),
                              display->getWidth() - display->getTextWidth(position.c_str()),
                              FOOTER_Y);
        }
        display->render();
        screenTimer.setTimer(screenMs);
    }

    PageManager* pages = nullptr;
    std::vector<std::vector<std::string>> screens;
    size_t screenIndex = 0;
    unsigned long screenMs = 1;
    SimpleTimer screenTimer;
    bool advanceRequested = false;
    bool finished = false;
};
