#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <cstdint>

namespace cli {

/**
 * Record of a single duel between two players.
 */
struct DuelRecord {
    std::string opponentId;      // Opponent's player ID or device index
    std::string gameName;        // Which minigame was played ("Quickdraw Duel")
    bool won;                    // Did this player win?
    uint32_t durationMs;         // How long the duel lasted
    uint32_t timestamp;          // When it happened (seconds since session start)
    uint32_t playerReactionMs;   // This player's reaction time
    uint32_t opponentReactionMs; // Opponent's reaction time

    DuelRecord() :
        opponentId(""),
        gameName("Quickdraw Duel"),
        won(false),
        durationMs(0),
        timestamp(0),
        playerReactionMs(0),
        opponentReactionMs(0)
    {
    }

    DuelRecord(const std::string& opponent, const std::string& game, bool victory,
               uint32_t duration, uint32_t ts, uint32_t playerRT, uint32_t opponentRT) :
        opponentId(opponent),
        gameName(game),
        won(victory),
        durationMs(duration),
        timestamp(ts),
        playerReactionMs(playerRT),
        opponentReactionMs(opponentRT)
    {
    }
};

/**
 * Tracks duel history and statistics for a CLI device.
 * Maintains a rolling history of the last 50 duels.
 */
class DuelHistory {
public:
    DuelHistory() = default;

    /**
     * Record a new duel result.
     * Automatically updates win/loss counts and streak tracking.
     */
    void recordDuel(const DuelRecord& record) {
        // Add to history (limit to last 50)
        records.push_back(record);
        if (records.size() > MAX_HISTORY_SIZE) {
            records.erase(records.begin());
        }

        // Update totals
        if (record.won) {
            totalWins++;
            if (currentStreak >= 0) {
                currentStreak++;
            } else {
                currentStreak = 1;
            }
            if (currentStreak > bestWinStreak) {
                bestWinStreak = currentStreak;
            }
        } else {
            totalLosses++;
            if (currentStreak <= 0) {
                currentStreak--;
            } else {
                currentStreak = -1;
            }
        }
    }

    /**
     * Get win rate as a percentage (0.0 to 1.0).
     */
    float getWinRate() const {
        uint32_t total = totalWins + totalLosses;
        if (total == 0) return 0.0f;
        return static_cast<float>(totalWins) / static_cast<float>(total);
    }

    /**
     * Get formatted history string (last N duels).
     */
    std::string getFormattedHistory(int count = 10) const {
        if (records.empty()) {
            return "No duel history";
        }

        std::string output;
        int startIdx = static_cast<int>(records.size()) - count;
        if (startIdx < 0) startIdx = 0;

        output += "Recent Duel History:\n";
        output += "  #  Opponent        Result  Time(ms)  Player RT  Opponent RT\n";
        output += "  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

        for (size_t i = startIdx; i < records.size(); i++) {
            const auto& r = records[i];
            char buf[128];
            snprintf(buf, sizeof(buf), "  %-2zu %-14s %-7s %-9u %-10u %u\n",
                     i + 1,
                     r.opponentId.c_str(),
                     r.won ? "WIN" : "LOSS",
                     r.durationMs,
                     r.playerReactionMs,
                     r.opponentReactionMs);
            output += buf;
        }

        return output;
    }

    /**
     * Get W-L record against a specific opponent.
     */
    std::string getVsRecord(const std::string& opponentId) const {
        int wins = 0;
        int losses = 0;

        for (const auto& r : records) {
            if (r.opponentId == opponentId) {
                if (r.won) {
                    wins++;
                } else {
                    losses++;
                }
            }
        }

        if (wins == 0 && losses == 0) {
            return "No history vs " + opponentId;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "%dW-%dL", wins, losses);
        return std::string(buf);
    }

    /**
     * Get overall record summary.
     */
    std::string getRecordSummary() const {
        char buf[256];
        const char* streakType = currentStreak > 0 ? "W" : "L";
        int streakAbs = currentStreak > 0 ? currentStreak : -currentStreak;

        snprintf(buf, sizeof(buf),
                 "Overall Record: %uW-%uL (%.1f%% win rate)\n"
                 "Current Streak: %d%s\n"
                 "Best Win Streak: %u",
                 totalWins, totalLosses, getWinRate() * 100.0f,
                 streakAbs, streakType,
                 bestWinStreak);

        return std::string(buf);
    }

    /**
     * Get the opponent ID from the most recent duel.
     * Returns empty string if no duels recorded.
     */
    std::string getLastOpponent() const {
        if (records.empty()) return "";
        return records.back().opponentId;
    }

    /**
     * Get the game name from the most recent duel.
     */
    std::string getLastGame() const {
        if (records.empty()) return "";
        return records.back().gameName;
    }

    /**
     * Check if the last duel was a win.
     */
    bool lastDuelWon() const {
        if (records.empty()) return false;
        return records.back().won;
    }

    // Accessors
    const std::vector<DuelRecord>& getRecords() const { return records; }
    uint16_t getTotalWins() const { return totalWins; }
    uint16_t getTotalLosses() const { return totalLosses; }
    int16_t getCurrentStreak() const { return currentStreak; }
    uint16_t getBestWinStreak() const { return bestWinStreak; }

private:
    static constexpr size_t MAX_HISTORY_SIZE = 50;

    std::vector<DuelRecord> records;
    uint16_t totalWins = 0;
    uint16_t totalLosses = 0;
    int16_t currentStreak = 0;   // Positive = wins, negative = losses
    uint16_t bestWinStreak = 0;
};

/**
 * Manages best-of-3 series state for a device.
 */
struct SeriesState {
    bool active = false;
    int playerWins = 0;
    int opponentWins = 0;
    std::string opponentId;
    std::vector<DuelRecord> seriesRecords;  // Games in this series

    void reset() {
        active = false;
        playerWins = 0;
        opponentWins = 0;
        opponentId.clear();
        seriesRecords.clear();
    }

    bool isComplete() const {
        return playerWins >= 2 || opponentWins >= 2;
    }

    bool playerWonSeries() const {
        return playerWins >= 2;
    }

    void recordGame(const DuelRecord& record) {
        seriesRecords.push_back(record);
        if (record.won) {
            playerWins++;
        } else {
            opponentWins++;
        }
    }

    std::string getSeriesScore() const {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d-%d", playerWins, opponentWins);
        return std::string(buf);
    }

    std::string getSeriesSummary() const {
        if (!active) return "No active series";

        std::string output;
        output += "Series vs " + opponentId + ": " + getSeriesScore();
        if (isComplete()) {
            output += " (" + std::string(playerWonSeries() ? "WIN" : "LOSS") + ")";
        }
        output += "\n";

        for (size_t i = 0; i < seriesRecords.size(); i++) {
            const auto& r = seriesRecords[i];
            char buf[128];
            snprintf(buf, sizeof(buf), "  Game %zu: %s (%ums)\n",
                     i + 1,
                     r.won ? "WIN" : "LOSS",
                     r.playerReactionMs);
            output += buf;
        }

        return output;
    }
};

} // namespace cli

#endif // NATIVE_BUILD
