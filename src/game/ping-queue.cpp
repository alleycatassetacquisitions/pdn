#include "game/ping-queue.hpp"
#include <algorithm>
#include <esp_log.h>

PingQueue::PingQueue() : 
    m_expiryTime(60000),  // 1 minute in ms (temporarily reduced from 5 minutes for testing)
    m_pingInterval(6000),  // 6 seconds in ms
    m_requiredPings(6) {
    ESP_LOGI("PingQueue", "PingQueue initialized");
    cleanupTimer.setTimer(m_expiryTime);
    
    // Add initial pings with staggered timestamps
    uint64_t currentTime = millis();
    const std::string initialSource = "INITIAL";
    
    // Add pings with m_pingInterval intervals
    for (int i = 0; i < m_requiredPings; i++) {
        uint64_t timestamp = currentTime - (i * m_pingInterval);
        m_pings.push_back({timestamp, initialSource});
        ESP_LOGI("PingQueue", "Added initial ping %d at time %llu", i, timestamp);
    }
}

void PingQueue::addPing(const std::string& sourceId) {
    uint64_t currentTime = millis();
    m_pings.push_back({currentTime, sourceId});
    ESP_LOGI("PingQueue", "Added ping for source %s at time %llu", sourceId.c_str(), currentTime);
}

void PingQueue::cleanup() {
    if (!cleanupTimer.expired()) {
        return;
    }
    
    uint64_t currentTime = millis();
    
    // Remove expired pings
    m_pings.erase(
        std::remove_if(m_pings.begin(), m_pings.end(),
            [currentTime, this](const Ping& ping) {
                return (currentTime - ping.timestamp) > m_expiryTime;
            }
        ),
        m_pings.end()
    );
    
    cleanupTimer.setTimer(m_expiryTime);
}

int PingQueue::getPingCount(const std::string& sourceId) const {
    return std::count_if(
        m_pings.begin(),
        m_pings.end(),
        [&sourceId](const Ping& ping) {
            return ping.sourceId == sourceId;
        }
    );
}

bool PingQueue::hasEnoughPings(const std::string& sourceId) const {
    return getValidPingCount(sourceId) >= m_requiredPings;
}

void PingQueue::clear() {
    m_pings.clear();
    cleanupTimer.invalidate();
}

int PingQueue::getValidPingCount(const std::string& sourceId) const {
    uint64_t currentTime = millis();
    return std::count_if(m_pings.begin(), m_pings.end(),
        [&sourceId, currentTime, this](const Ping& ping) {
            return ping.sourceId == sourceId && 
                   (currentTime - ping.timestamp) <= m_expiryTime;
        }
    );
}

std::set<std::string> PingQueue::getActiveSources() const {
    std::set<std::string> activeSources;
    uint64_t currentTime = millis();
    
    for (const auto& ping : m_pings) {
        if ((currentTime - ping.timestamp) <= m_expiryTime) {
            activeSources.insert(ping.sourceId);
        }
    }
    
    return activeSources;
}

bool PingQueue::isSourceActive(const std::string& sourceId) const {
    uint64_t currentTime = millis();
    return std::any_of(m_pings.begin(), m_pings.end(),
        [&sourceId, currentTime, this](const Ping& ping) {
            return ping.sourceId == sourceId && 
                   (currentTime - ping.timestamp) <= m_expiryTime;
        }
    );
}

uint64_t PingQueue::getLastPingTime(const std::string& sourceId) const {
    uint64_t lastPingTime = 0;
    
    for (const auto& ping : m_pings) {
        if (ping.sourceId == sourceId && ping.timestamp > lastPingTime) {
            lastPingTime = ping.timestamp;
        }
    }
    
    return lastPingTime;
}

uint64_t PingQueue::getTimeSinceLastPing(const std::string& sourceId) const {
    uint64_t lastPingTime = getLastPingTime(sourceId);
    if (lastPingTime == 0) {
        return UINT64_MAX;
    }
    return millis() - lastPingTime;
} 