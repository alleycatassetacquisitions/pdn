#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "device/drivers/logger.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/page-cmd.hpp"

static const char* PAGE_MGR_TAG = "PageManager";

/**
 * Mission Control page inbox: recent page ids, local-player filter, and a short
 * queue of pages waiting for an idle-safe state to show them, oldest first. The
 * PDN feeds it from ESP-NOW (processPacket); the FDN feeds it through PageRelay.
 */
class PageManager {
public:
    static constexpr size_t DEDUPE_RING_SIZE = 16;
    /** Pages held while the device is busy; a new page past this drops the oldest. */
    static constexpr size_t MAX_QUEUED = 5;
    /** A page not shown within this long (e.g. a very long match) is dropped unseen. */
    static constexpr unsigned long MAX_HOLD_MS = 10UL * 60UL * 1000UL;
    /** ESP-NOW v2 payload budget is ~1469 bytes after DataPktHdr; keep a margin. */
    static constexpr size_t MAX_OPAQUE_PAYLOAD = 1400;

    /** PDN: the player pages are filtered against. Null (FDN) accepts every page. */
    void setLocalPlayer(Player* player) { localPlayer = player; }

    /** PDN path: opaque PageCmd bytes from ESP-NOW. */
    int processPacket(const uint8_t* /*srcMac*/, const uint8_t* data, size_t length) {
        if (length > MAX_OPAQUE_PAYLOAD) {
            LOG_W(PAGE_MGR_TAG, "Reject ESP-Now page: %u bytes", static_cast<unsigned>(length));
            return -1;
        }
        PageCmd cmd;
        if (!parsePageCmdJson(data, length, cmd)) {
            LOG_W(PAGE_MGR_TAG, "Reject ESP-Now page: invalid JSON");
            return -1;
        }
        if (hasSeen(cmd.id)) {
            LOG_D(PAGE_MGR_TAG, "Skip duplicate page id %s", cmd.id.c_str());
            return 0;
        }
        // Filtered ids are not remembered, so they never evict ones this player saw.
        if (!pageCmdMatchesPlayer(cmd, localPlayer)) {
            LOG_D(PAGE_MGR_TAG, "Page filtered for local player");
            return 0;
        }
        remember(cmd.id);
        queuePending(cmd);
        return 0;
    }

    bool hasSeen(const std::string& id) const {
        if (id.empty()) {
            return false;
        }
        for (size_t i = 0; i < dedupeCount; ++i) {
            if (dedupeRing[i] == id) {
                return true;
            }
        }
        return false;
    }

    void remember(const std::string& id) {
        if (id.empty()) {
            return;
        }
        dedupeRing[dedupeNext] = id;
        dedupeNext = (dedupeNext + 1) % DEDUPE_RING_SIZE;
        if (dedupeCount < DEDUPE_RING_SIZE) {
            ++dedupeCount;
        }
    }

    /** Adds a page to the back of the queue, dropping the oldest when full. */
    void queuePending(const PageCmd& cmd) {
        if (queue.size() >= MAX_QUEUED) {
            LOG_W(PAGE_MGR_TAG, "Page queue full; dropping oldest id=%s",
                  queue.front().cmd.id.c_str());
            queue.pop_front();
        }
        QueuedPage entry;
        entry.cmd = cmd;
        entry.hold.setTimer(MAX_HOLD_MS);
        queue.push_back(entry);
        LOG_I(PAGE_MGR_TAG, "Queued page id=%s duration=%ds (%u waiting)", cmd.id.c_str(),
              cmd.durationSeconds, static_cast<unsigned>(queue.size()));
    }

    /** True when a page is waiting. Drops pages held longer than MAX_HOLD_MS. */
    bool hasPendingPage() {
        dropStale();
        return !queue.empty();
    }

    /** Pages waiting, after dropping stale ones. */
    size_t pendingCount() {
        dropStale();
        return queue.size();
    }

    /** Moves the oldest waiting page into `out`. False when none is waiting. */
    bool takePendingPage(PageCmd& out) {
        if (!hasPendingPage()) {
            return false;
        }
        out = queue.front().cmd;
        queue.pop_front();
        return true;
    }

private:
    struct QueuedPage {
        PageCmd cmd;
        SimpleTimer hold;
    };

    /** Every page gets the same hold, so the oldest always expires first. */
    void dropStale() {
        while (!queue.empty() && queue.front().hold.expired()) {
            LOG_I(PAGE_MGR_TAG, "Drop stale page id=%s", queue.front().cmd.id.c_str());
            queue.pop_front();
        }
    }

    Player* localPlayer = nullptr;
    std::deque<QueuedPage> queue;
    std::array<std::string, DEDUPE_RING_SIZE> dedupeRing{};
    size_t dedupeCount = 0;
    size_t dedupeNext = 0;
};
