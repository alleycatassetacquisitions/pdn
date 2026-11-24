#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "device/drivers/logger.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/page-cmd.hpp"

static const char* PAGE_MGR_TAG = "PageManager";

/**
 * Mission Control page inbox: recent page ids, local-player filter, and the one
 * page waiting for an idle-safe state to show it. The PDN feeds it from ESP-NOW
 * (processPacket); the FDN feeds it through PageRelay.
 */
class PageManager {
public:
    static constexpr size_t DEDUPE_RING_SIZE = 16;
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

    /** Replaces any page still waiting. It expires after its own duration if never shown. */
    void queuePending(const PageCmd& cmd) {
        pending = cmd;
        hasPending = true;
        const int waitSeconds = cmd.durationSeconds > 0 ? cmd.durationSeconds : 1;
        pendingExpiry.setTimer(static_cast<unsigned long>(waitSeconds) * 1000UL);
        LOG_I(PAGE_MGR_TAG, "Pending page id=%s duration=%ds", pending.id.c_str(),
              pending.durationSeconds);
    }

    /** True when a page is waiting. Drops one that waited out its whole duration. */
    bool hasPendingPage() {
        if (hasPending && pendingExpiry.expired()) {
            LOG_I(PAGE_MGR_TAG, "Drop stale page id=%s", pending.id.c_str());
            clearPending();
        }
        return hasPending;
    }

    /** Moves the waiting page into `out`. False when none is waiting. */
    bool takePendingPage(PageCmd& out) {
        if (!hasPendingPage()) {
            return false;
        }
        out = pending;
        clearPending();
        return true;
    }

private:
    void clearPending() {
        hasPending = false;
        pending = PageCmd{};
        pendingExpiry.invalidate();
    }

    Player* localPlayer = nullptr;
    PageCmd pending;
    bool hasPending = false;
    SimpleTimer pendingExpiry;
    std::array<std::string, DEDUPE_RING_SIZE> dedupeRing{};
    size_t dedupeCount = 0;
    size_t dedupeNext = 0;
};
