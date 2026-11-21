#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "game/player.hpp"
#include "wireless/page-cmd.hpp"

static const char* PAGE_MGR_TAG = "PageManager";

/**
 * Owns page dedupe, optional local filter, pending display, and opaque ESP-Now relay.
 * FDN: ingestMqttPayload (no filter) → display + broadcast. PDN: processPacket (filter).
 */
class PageManager {
public:
    static constexpr size_t DEDUPE_RING_SIZE = 16;
    /** ESP-NOW v2 app payload budget minus DataPktHdr is ~1469; keep a margin. */
    static constexpr size_t MAX_OPAQUE_PAYLOAD = 1400;

    void initialize(WirelessManager* wirelessManager, Player* localPlayer) {
        this->wirelessManager = wirelessManager;
        this->localPlayer = localPlayer;
        dedupeCount = 0;
        dedupeNext = 0;
        hasPending = false;
        pending = PageCmd{};
    }

    /**
     * FDN path: parse/validate, dedupe, queue display, opaque-broadcast MQTT bytes.
     * @return false when payload is invalid, duplicate, or too large to send.
     */
    bool ingestMqttPayload(const uint8_t* data, size_t length) {
        PageCmd cmd;
        if (!parsePageCmdJson(data, length, cmd)) {
            LOG_W(PAGE_MGR_TAG, "Reject MQTT page: invalid JSON");
            return false;
        }
        if (length > MAX_OPAQUE_PAYLOAD) {
            LOG_W(PAGE_MGR_TAG, "Reject MQTT page: payload %u > %u",
                  static_cast<unsigned>(length), static_cast<unsigned>(MAX_OPAQUE_PAYLOAD));
            return false;
        }
        if (!cmd.id.empty() && seenRecently(cmd.id)) {
            LOG_D(PAGE_MGR_TAG, "Skip duplicate page id %s", cmd.id.c_str());
            return false;
        }
        if (!cmd.id.empty()) {
            rememberId(cmd.id);
        }
        queuePending(cmd);
        return broadcastOpaque(data, length) == 0;
    }

    /** PDN path: decode opaque PageCmd bytes from ESP-Now. */
    int processPacket(const uint8_t* /*srcMac*/, const uint8_t* data, size_t length) {
        PageCmd cmd;
        if (!parsePageCmdJson(data, length, cmd)) {
            LOG_W(PAGE_MGR_TAG, "Reject ESP-Now page: invalid JSON");
            return -1;
        }
        if (!cmd.id.empty() && seenRecently(cmd.id)) {
            LOG_D(PAGE_MGR_TAG, "Skip duplicate page id %s", cmd.id.c_str());
            return 0;
        }
        if (!pageCmdMatchesPlayer(cmd, localPlayer)) {
            LOG_D(PAGE_MGR_TAG, "Page filtered for local player");
            return 0;
        }
        if (!cmd.id.empty()) {
            rememberId(cmd.id);
        }
        queuePending(cmd);
        return 0;
    }

    bool hasPendingPage() const { return hasPending; }

    const PageCmd& pendingPage() const { return pending; }

    void clearPendingPage() {
        hasPending = false;
        pending = PageCmd{};
    }

    /**
     * Queue a page for display without broadcasting (tests / FDN local-only retry).
     * Still applies dedupe when id is set.
     */
    bool queueForDisplay(const PageCmd& cmd) {
        if (!cmd.id.empty() && seenRecently(cmd.id)) {
            return false;
        }
        if (!cmd.id.empty()) {
            rememberId(cmd.id);
        }
        queuePending(cmd);
        return true;
    }

private:
    void queuePending(const PageCmd& cmd) {
        pending = cmd;
        hasPending = true;
        LOG_I(PAGE_MGR_TAG, "Pending page id=%s duration=%ds", pending.id.c_str(),
              pending.durationSeconds);
    }

    int broadcastOpaque(const uint8_t* data, size_t length) {
        if (wirelessManager == nullptr) {
            return -1;
        }
        const uint8_t* broadcast = wirelessManager->getBroadcastAddress();
        if (broadcast == nullptr) {
            return -1;
        }
        const int rc = wirelessManager->sendEspNowData(
            broadcast, PktType::kPageCommand, data, length);
        if (rc != 0) {
            LOG_W(PAGE_MGR_TAG, "ESP-Now page broadcast failed: %d", rc);
        }
        return rc;
    }

    bool seenRecently(const std::string& id) const {
        for (size_t i = 0; i < dedupeCount; ++i) {
            if (dedupeRing[i] == id) {
                return true;
            }
        }
        return false;
    }

    void rememberId(const std::string& id) {
        dedupeRing[dedupeNext] = id;
        dedupeNext = (dedupeNext + 1) % DEDUPE_RING_SIZE;
        if (dedupeCount < DEDUPE_RING_SIZE) {
            ++dedupeCount;
        }
    }

    WirelessManager* wirelessManager = nullptr;
    Player* localPlayer = nullptr;
    PageCmd pending;
    bool hasPending = false;
    std::array<std::string, DEDUPE_RING_SIZE> dedupeRing{};
    size_t dedupeCount = 0;
    size_t dedupeNext = 0;
};
