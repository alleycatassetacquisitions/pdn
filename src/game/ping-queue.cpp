#include "game/ping-queue.hpp"
#include <esp_log.h>

PingQueue::PingQueue() {
    ESP_LOGI("PingQueue", "PingQueue initialized");
}

PingQueue::~PingQueue() {
    clear();
}

void PingQueue::addPing(const std::string& sourceId) {
    uint64_t currentTime = esp_timer_get_time() / 1000; // Convert to milliseconds
    pings[sourceId].push_back({currentTime, true});
    ESP_LOGI("PingQueue", "Added ping for source %s at time %llu", sourceId.c_str(), currentTime);
}

void PingQueue::cleanup() {
    uint64_t currentTime = esp_timer_get_time() / 1000;
    
    for (auto& [sourceId, pingList] : pings) {
        // Remove expired pings
        pingList.erase(
            std::remove_if(pingList.begin(), pingList.end(),
                [currentTime](const PingInfo& ping) {
                    return (currentTime - ping.timestamp) > PING_TIMEOUT_MS;
                }
            ),
            pingList.end()
        );
    }
}

int PingQueue::getPingCount(const std::string& sourceId) const {
    auto it = pings.find(sourceId);
    if (it == pings.end()) {
        return 0;
    }
    return it->second.size();
}

bool PingQueue::hasEnoughPings(const std::string& sourceId) const {
    return getValidPingCount(sourceId) >= MIN_PINGS_REQUIRED;
}

void PingQueue::clear() {
    pings.clear();
}

int PingQueue::getValidPingCount(const std::string& sourceId) const {
    auto it = pings.find(sourceId);
    if (it == pings.end()) {
        return 0;
    }
    
    uint64_t currentTime = esp_timer_get_time() / 1000;
    return std::count_if(it->second.begin(), it->second.end(),
        [currentTime](const PingInfo& ping) {
            return ping.valid && (currentTime - ping.timestamp) <= PING_TIMEOUT_MS;
        }
    );
}

std::set<std::string> PingQueue::getActiveSources() const {
    std::set<std::string> activeSources;
    uint64_t currentTime = esp_timer_get_time() / 1000;
    
    for (const auto& [sourceId, pingList] : pings) {
        if (std::any_of(pingList.begin(), pingList.end(),
            [currentTime](const PingInfo& ping) {
                return ping.valid && (currentTime - ping.timestamp) <= PING_TIMEOUT_MS;
            }
        )) {
            activeSources.insert(sourceId);
        }
    }
    
    return activeSources;
}

bool PingQueue::isSourceActive(const std::string& sourceId) const {
    return getValidPingCount(sourceId) > 0;
}

uint64_t PingQueue::getLastPingTime(const std::string& sourceId) const {
    auto it = pings.find(sourceId);
    if (it == pings.end() || it->second.empty()) {
        return 0;
    }
    
    return it->second.back().timestamp;
}

uint64_t PingQueue::getTimeSinceLastPing(const std::string& sourceId) const {
    uint64_t lastPingTime = getLastPingTime(sourceId);
    if (lastPingTime == 0) {
        return UINT64_MAX;
    }
    
    return (esp_timer_get_time() / 1000) - lastPingTime;
} 