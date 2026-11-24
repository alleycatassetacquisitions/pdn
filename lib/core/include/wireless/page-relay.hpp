#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/page-cmd.hpp"
#include "wireless/page-manager.hpp"

static const char* PAGE_RELAY_TAG = "PageRelay";

/**
 * FDN side of paging: takes PageCmd bytes from MQTT, shows them locally, and
 * re-broadcasts the exact bytes to nearby PDNs as kPageCommand. Pages queue on
 * ingest() and go out together on flush(), so a burst costs one channel hop.
 */
class PageRelay {
public:
    /** Pages held between flushes; more are rejected until the next flush. */
    static constexpr size_t MAX_BATCH = 8;

    void initialize(WirelessManager* wirelessManager, PageManager* inbox) {
        this->wirelessManager = wirelessManager;
        this->inbox = inbox;
        outbox.clear();
    }

    /** @return false when the payload is too large, invalid, a duplicate, or the batch is full. */
    bool ingest(const uint8_t* data, size_t length) {
        if (length > PageManager::MAX_OPAQUE_PAYLOAD) {
            LOG_W(PAGE_RELAY_TAG, "Reject MQTT page: payload %u > %u",
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(PageManager::MAX_OPAQUE_PAYLOAD));
            return false;
        }
        PageCmd cmd;
        if (!parsePageCmdJson(data, length, cmd)) {
            LOG_W(PAGE_RELAY_TAG, "Reject MQTT page: invalid JSON");
            return false;
        }
        if (inbox->hasSeen(cmd.id) || isQueued(cmd.id)) {
            LOG_D(PAGE_RELAY_TAG, "Skip duplicate page id %s", cmd.id.c_str());
            return false;
        }
        if (outbox.size() >= MAX_BATCH) {
            LOG_W(PAGE_RELAY_TAG, "Reject MQTT page id %s: relay batch full", cmd.id.c_str());
            return false;
        }
        inbox->queuePending(cmd);
        outbox.push_back({cmd.id, std::vector<uint8_t>(data, data + length)});
        return true;
    }

    /**
     * Broadcasts every queued page on ESPNOW_CHANNEL. An id is remembered only once
     * its send is accepted, so Mission Control can retry a page whose relay failed.
     */
    void flush() {
        if (outbox.empty()) {
            return;
        }
        const uint8_t* broadcast = wirelessManager->getBroadcastAddress();
        wirelessManager->runOnEspNowChannel([this, broadcast]() {
            for (const QueuedPage& page : outbox) {
                const int rc = broadcast == nullptr
                    ? -1
                    : wirelessManager->sendEspNowData(broadcast, PktType::kPageCommand,
                                                      page.bytes.data(), page.bytes.size());
                if (rc == 0) {
                    inbox->remember(page.id);
                    LOG_I(PAGE_RELAY_TAG, "Relayed page id=%s (%u bytes)", page.id.c_str(),
                          static_cast<unsigned>(page.bytes.size()));
                } else {
                    LOG_W(PAGE_RELAY_TAG, "Relay failed for page id=%s: %d", page.id.c_str(), rc);
                }
            }
        });
        outbox.clear();
    }

private:
    struct QueuedPage {
        std::string id;
        std::vector<uint8_t> bytes;
    };

    bool isQueued(const std::string& id) const {
        if (id.empty()) {
            return false;
        }
        for (const QueuedPage& page : outbox) {
            if (page.id == id) {
                return true;
            }
        }
        return false;
    }

    WirelessManager* wirelessManager = nullptr;
    PageManager* inbox = nullptr;
    std::vector<QueuedPage> outbox;
};
