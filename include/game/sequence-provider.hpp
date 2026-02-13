#pragma once

#include "device/device-types.hpp"
#include "device/drivers/http-client-interface.hpp"
#include <vector>
#include <string>
#include <functional>
#include <map>

/*
 * Difficulty levels for minigames.
 * Used to request appropriate sequences from server or local generation.
 */
enum class Difficulty {
    EASY,
    HARD
};

/*
 * SequenceData — Generic container for game-specific sequence data.
 * Different games use different formats:
 * - Signal Echo: vector of bools (UP/DOWN)
 * - Ghost Runner: 2D maze grid
 * - Others: TBD
 */
struct SequenceData {
    GameType gameType;
    Difficulty difficulty;
    std::vector<bool> boolSequence;           // For Signal Echo
    std::vector<std::vector<int>> grid;       // For maze-based games
    unsigned long seed;                       // RNG seed used for generation
    int version;                              // API version for compatibility

    SequenceData() : gameType(GameType::SIGNAL_ECHO), difficulty(Difficulty::EASY), seed(0), version(1) {}
};

/*
 * SequenceProvider — Abstract interface for sequence generation.
 *
 * Implementations:
 * - LocalSequenceProvider: PRNG fallback using local logic
 * - ServerSequenceProvider: HTTP fetch from remote endpoint
 * - HybridSequenceProvider: Server-first with local fallback
 */
class SequenceProvider {
public:
    virtual ~SequenceProvider() = default;

    /*
     * Fetch a sequence for the given game type and difficulty.
     * Returns true if sequence was successfully generated/fetched.
     * Fills out 'data' parameter with the sequence.
     */
    virtual bool fetchSequence(GameType gameType, Difficulty difficulty, SequenceData& data) = 0;
};

/*
 * LocalSequenceProvider — Generates sequences using local PRNG.
 * This is the fallback provider that wraps existing game logic.
 */
class LocalSequenceProvider : public SequenceProvider {
public:
    explicit LocalSequenceProvider(unsigned long rngSeed = 0);
    ~LocalSequenceProvider() override = default;

    bool fetchSequence(GameType gameType, Difficulty difficulty, SequenceData& data) override;

private:
    unsigned long rngSeed;

    // Game-specific generation methods
    std::vector<bool> generateSignalEchoSequence(int length);
    std::vector<std::vector<int>> generateGhostRunnerMaze(int size);
};

/*
 * ServerSequenceProvider — Fetches sequences from HTTP endpoint.
 *
 * Endpoint: GET /api/sequences?game={game}&difficulty={difficulty}
 * Response: { "sequence": [...], "seed": N, "version": 1 }
 *
 * Timeout: 2 seconds. On timeout/error, returns false.
 */
class ServerSequenceProvider : public SequenceProvider {
public:
    explicit ServerSequenceProvider(HttpClientInterface* httpClient);
    ~ServerSequenceProvider() override = default;

    bool fetchSequence(GameType gameType, Difficulty difficulty, SequenceData& data) override;

    /*
     * Timeout for HTTP requests in milliseconds.
     * Default: 2000ms (2 seconds)
     */
    void setTimeout(unsigned long timeoutMs) { this->timeoutMs = timeoutMs; }
    unsigned long getTimeout() const { return timeoutMs; }

private:
    HttpClientInterface* httpClient;
    unsigned long timeoutMs = 2000;

    // Track request state
    bool requestInProgress = false;
    bool requestCompleted = false;
    bool requestSucceeded = false;
    std::string lastResponse;

    // Callbacks for HTTP request
    void onHttpSuccess(const std::string& response);
    void onHttpError(const WirelessErrorInfo& error);

    // Parse JSON response into SequenceData
    bool parseResponse(const std::string& response, GameType gameType, SequenceData& data);

    // Convert game type/difficulty to API parameters
    std::string gameTypeToString(GameType type);
    std::string difficultyToString(Difficulty difficulty);
};

/*
 * HybridSequenceProvider — Server-first with local fallback.
 *
 * Attempts to fetch from server first. If that fails (timeout, error,
 * or HTTP client not connected), falls back to local PRNG generation.
 *
 * This is the recommended provider for production use.
 */
class HybridSequenceProvider : public SequenceProvider {
public:
    HybridSequenceProvider(HttpClientInterface* httpClient, unsigned long rngSeed = 0);
    ~HybridSequenceProvider() override = default;

    bool fetchSequence(GameType gameType, Difficulty difficulty, SequenceData& data) override;

    void setTimeout(unsigned long timeoutMs) { serverProvider.setTimeout(timeoutMs); }

    /*
     * Statistics for debugging/monitoring
     */
    int getServerSuccessCount() const { return serverSuccesses; }
    int getLocalFallbackCount() const { return localFallbacks; }

private:
    ServerSequenceProvider serverProvider;
    LocalSequenceProvider localProvider;

    int serverSuccesses = 0;
    int localFallbacks = 0;
};

/*
 * SequenceCache — Simple cache for sequences.
 * Stores last-fetched sequences for offline use.
 *
 * Key: (GameType, Difficulty) -> SequenceData
 *
 * This allows the system to use previously-fetched server sequences
 * when offline, rather than falling back to pure PRNG.
 */
class SequenceCache {
public:
    SequenceCache() = default;

    void store(GameType gameType, Difficulty difficulty, const SequenceData& data);
    bool retrieve(GameType gameType, Difficulty difficulty, SequenceData& data);
    bool hasEntry(GameType gameType, Difficulty difficulty);
    void clear();

private:
    struct CacheKey {
        GameType gameType;
        Difficulty difficulty;

        bool operator<(const CacheKey& other) const {
            if (gameType != other.gameType) return gameType < other.gameType;
            return difficulty < other.difficulty;
        }
    };

    std::map<CacheKey, SequenceData> cache;
};
