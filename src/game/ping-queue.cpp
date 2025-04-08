#include "game/ping-queue.hpp"
#include <algorithm>
#include <esp_log.h>

void PingQueue::addPing(const std::string& sourceId) {
    uint64_t currentTime = millis();
    
    // Add new ping
    m_pings.push_back({currentTime, sourceId});
    
    // Clean up expired pings
    cleanup();
}

void PingQueue::cleanup() {
    uint64_t currentTime = millis();
    
    // Remove expired pings
    m_pings.erase(
        std::remove_if(m_pings.begin(), m_pings.end(),
            [currentTime, this](const Ping& ping) {
                return (currentTime - ping.timestamp) > m_expiryTime;
            }),
        m_pings.end()
    );
}

int PingQueue::getPingCount(const std::string& sourceId) const {
    return std::count_if(m_pings.begin(), m_pings.end(),
        [&sourceId](const Ping& ping) {
            return ping.sourceId == sourceId;
        });
}

bool PingQueue::hasEnoughPings(const std::string& sourceId) const {
    return getValidPingCount(sourceId) >= m_requiredPings;
}

void PingQueue::clear() {
    m_pings.clear();
}

int PingQueue::getValidPingCount(const std::string& sourceId) const {
    uint64_t currentTime = millis();
    
    return std::count_if(m_pings.begin(), m_pings.end(),
        [&sourceId, currentTime, this](const Ping& ping) {
            return ping.sourceId == sourceId && 
                   (currentTime - ping.timestamp) <= m_expiryTime;
        });
}

std::set<std::string> PingQueue::getActiveSources() const {
    std::set<std::string> activeSources;
    uint64_t currentTime = millis();
    
    for (const auto& ping : m_pings) {
        if (currentTime - ping.timestamp <= m_expiryTime) {
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
        });
}

uint64_t PingQueue::getLastPingTime(const std::string& sourceId) const {
    uint64_t lastTime = 0;
    
    for (const auto& ping : m_pings) {
        if (ping.sourceId == sourceId && ping.timestamp > lastTime) {
            lastTime = ping.timestamp;
        }
    }
    
    return lastTime;
}

uint64_t PingQueue::getTimeSinceLastPing(const std::string& sourceId) const {
    uint64_t lastPingTime = getLastPingTime(sourceId);
    if (lastPingTime == 0) {
        return m_expiryTime; // Return max time if no pings found
    }
    
    return millis() - lastPingTime;
} 