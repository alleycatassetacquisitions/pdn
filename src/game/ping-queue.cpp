#include "game/ping-queue.hpp"
#include <algorithm>
#include <esp_log.h>

void PingQueue::addPing(const std::string& sourceId) {
    uint64_t currentTime = millis();
    m_pings.push_back({currentTime, sourceId});
}

void PingQueue::cleanup() {
    uint64_t currentTime = millis();
    m_pings.erase(
        std::remove_if(
            m_pings.begin(),
            m_pings.end(),
            [currentTime, this](const Ping& ping) {
                return (currentTime - ping.timestamp) > m_expiryTime;
            }
        ),
        m_pings.end()
    );
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
    return getPingCount(sourceId) >= m_requiredPings;
}

void PingQueue::clear() {
    m_pings.clear();
}

int PingQueue::getValidPingCount(const std::string& sourceId) const {
    uint64_t currentTime = millis();
    return std::count_if(
        m_pings.begin(),
        m_pings.end(),
        [&sourceId, currentTime, this](const Ping& ping) {
            return ping.sourceId == sourceId && 
                   (currentTime - ping.timestamp) <= m_expiryTime;
        }
    );
}

std::set<std::string> PingQueue::getActiveSources() const {
    std::set<std::string> sources;
    uint64_t currentTime = millis();
    
    for (const auto& ping : m_pings) {
        if ((currentTime - ping.timestamp) <= m_expiryTime) {
            sources.insert(ping.sourceId);
        }
    }
    
    return sources;
}

bool PingQueue::isSourceActive(const std::string& sourceId) const {
    uint64_t currentTime = millis();
    return std::any_of(
        m_pings.begin(),
        m_pings.end(),
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