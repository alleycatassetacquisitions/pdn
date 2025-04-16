#pragma once

#include <string>
#include <map>
#include <set>
#include <chrono>

class PingQueue {
public:
    PingQueue();
    ~PingQueue();

    void addPing(const std::string& sourceId);
    void cleanup();
    int getPingCount(const std::string& sourceId) const;
    bool hasEnoughPings(const std::string& sourceId) const;
    void clear();
    int getValidPingCount(const std::string& sourceId) const;
    std::set<std::string> getActiveSources() const;
    bool isSourceActive(const std::string& sourceId) const;
    uint64_t getLastPingTime(const std::string& sourceId) const;
    uint64_t getTimeSinceLastPing(const std::string& sourceId) const;

private:
    struct PingInfo {
        uint64_t timestamp;
        bool valid;
    };

    std::map<std::string, std::vector<PingInfo>> pings;
    static constexpr uint64_t PING_TIMEOUT_MS = 5000; // 5 seconds
    static constexpr int MIN_PINGS_REQUIRED = 3;
}; 