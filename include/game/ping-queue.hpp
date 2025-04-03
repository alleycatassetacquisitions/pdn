#pragma once

#include <vector>
#include <string>
#include <set>
#include <Arduino.h>

class PingQueue {
public:
    struct Ping {
        uint64_t timestamp;
        std::string sourceId;
    };

    PingQueue() : 
        m_expiryTime(60000),  // 1 minute in ms (temporarily reduced from 5 minutes for testing)
        m_pingInterval(6000),  // 6 seconds in ms
        m_requiredPings(6) {}

    // Add a new ping from a source
    void addPing(const std::string& sourceId);

    // Clean up expired pings
    void cleanup();

    // Get the number of valid pings from a source
    int getPingCount(const std::string& sourceId) const;

    // Check if a source has enough pings
    bool hasEnoughPings(const std::string& sourceId) const;

    // Clear all pings
    void clear();

    // Get the current number of valid pings for a source
    int getValidPingCount(const std::string& sourceId) const;

    // Get all unique sources that have pinged in the last 5 minutes
    std::set<std::string> getActiveSources() const;

    // Check if a specific source has been active in the last 5 minutes
    bool isSourceActive(const std::string& sourceId) const;

    // Get the most recent ping from a source
    uint64_t getLastPingTime(const std::string& sourceId) const;

    // Get the time since the last ping from a source
    uint64_t getTimeSinceLastPing(const std::string& sourceId) const;

private:
    std::vector<Ping> m_pings;
    const uint64_t m_expiryTime;
    const uint64_t m_pingInterval;
    const int m_requiredPings;
}; 