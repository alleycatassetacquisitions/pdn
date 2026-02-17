#ifdef NATIVE_BUILD

#include "cli/commands/stats-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "game/player.hpp"
#include "game/game-stats.hpp"
#include <cstdio>

namespace cli {

CommandResult StatsCommands::cmdStats(const std::vector<std::string>& tokens,
                                      std::vector<DeviceInstance>& devices,
                                      int selectedDevice) {
    CommandResult result;

    // Parse subcommand if present
    std::string subcommand = tokens.size() >= 2 ? tokens[1] : "summary";
    for (char& c : subcommand) {
        if (c >= 'A' && c <= 'Z') c += 32;  // tolower
    }

    // Check for reset subcommand
    if (subcommand == "reset") {
        return cmdStatsReset(tokens, devices, selectedDevice);
    }

    // Determine target device - check if first arg is a device ID
    int targetDevice = selectedDevice;
    int argOffset = 1;  // Start checking from token[1]

    // If subcommand is a valid device ID, use it
    int tempDevice = CommandUtils::findDevice(subcommand, devices, -1);
    if (tempDevice >= 0) {
        targetDevice = tempDevice;
        subcommand = (tokens.size() >= 3) ? tokens[2] : "summary";
        for (char& c : subcommand) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        argOffset = 2;
    }

    // Validate target device
    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device";
        return result;
    }

    auto& dev = devices[targetDevice];

    // Check if device has a player (NPC/FDN devices don't)
    if (!dev.player) {
        result.message = "NPC device - no player stats";
        return result;
    }

    // Route to appropriate subcommand handler
    if (subcommand == "detailed") {
        return cmdStatsDetailed(dev, targetDevice);
    } else if (subcommand == "streaks") {
        return cmdStatsStreaks(dev, targetDevice);
    } else {
        // Default: summary
        return cmdStatsSummary(dev, targetDevice);
    }
}

CommandResult StatsCommands::cmdStatsSummary(DeviceInstance& dev, int deviceIndex) {
    CommandResult result;
    Player* p = dev.player;
    const GameStatsTracker& tracker = p->getGameStatsTracker();

    // Check if any games have been played
    if (!tracker.hasAnyGamesPlayed()) {
        result.message = "No FDN games played yet. Connect to an FDN device to start playing!";
        return result;
    }

    // Build table
    std::string table;
    table += "\n╔════════════════════════╦════════╦════════╦══════════╦══════════╦════════════╗\n";
    table += "║ Game                   ║  W/L   ║ Win %  ║ Avg Time ║ Best Time║   Streak   ║\n";
    table += "╠════════════════════════╬════════╬════════╬══════════╬══════════╬════════════╣\n";

    const GameType games[] = {
        GameType::GHOST_RUNNER, GameType::SPIKE_VECTOR, GameType::FIREWALL_DECRYPT,
        GameType::CIPHER_PATH, GameType::EXPLOIT_SEQUENCER, GameType::BREACH_DEFENSE,
        GameType::SIGNAL_ECHO
    };

    uint16_t totalWins = 0, totalLosses = 0;
    uint32_t overallBestTime = UINT32_MAX;
    uint16_t maxCurrentStreak = 0;
    uint16_t maxBestStreak = 0;
    bool hasCurrentWinStreak = false;

    for (const GameType& game : games) {
        const GameStats* combined = tracker.getCombinedStats(game);
        if (!combined || combined->totalAttempts == 0) continue;

        totalWins += combined->wins;
        totalLosses += combined->losses;

        if (combined->bestTimeMs < overallBestTime) {
            overallBestTime = combined->bestTimeMs;
        }

        if (combined->currentWinStreak > maxCurrentStreak) {
            maxCurrentStreak = combined->currentWinStreak;
            hasCurrentWinStreak = true;
        }

        if (combined->bestWinStreak > maxBestStreak) {
            maxBestStreak = combined->bestWinStreak;
        }

        // Format game name (pad to 22 chars)
        std::string gameName = getGameDisplayName(game);
        while (gameName.length() < 22) gameName += " ";

        // W/L (pad to 6 chars)
        char wlBuf[16];
        snprintf(wlBuf, sizeof(wlBuf), "%2d/%2d", combined->wins, combined->losses);
        std::string wl = wlBuf;
        while (wl.length() < 6) wl += " ";

        // Win % (pad to 6 chars)
        char winPctBuf[16];
        float winPct = tracker.getCombinedWinRate(game) * 100.0f;
        snprintf(winPctBuf, sizeof(winPctBuf), "%5.1f%%", winPct);
        std::string winPctStr = winPctBuf;

        // Avg Time (pad to 8 chars)
        uint32_t avgTime = tracker.getCombinedAverageTime(game);
        char avgBuf[16];
        snprintf(avgBuf, sizeof(avgBuf), "%5.1fs", avgTime / 1000.0f);
        std::string avgStr = avgBuf;
        while (avgStr.length() < 8) avgStr = " " + avgStr;

        // Best Time (pad to 8 chars)
        uint32_t bestTime = tracker.getCombinedBestTime(game);
        char bestBuf[16];
        if (bestTime == UINT32_MAX) {
            snprintf(bestBuf, sizeof(bestBuf), "    --");
        } else {
            snprintf(bestBuf, sizeof(bestBuf), "%5.1fs", bestTime / 1000.0f);
        }
        std::string bestStr = bestBuf;
        while (bestStr.length() < 8) bestStr = " " + bestStr;

        // Streak (pad to 10 chars)
        char streakBuf[32];
        char currentStreakChar = (combined->currentWinStreak > 0) ? 'W' : 'L';
        uint16_t currentStreakVal = (combined->currentWinStreak > 0) ?
                                    combined->currentWinStreak : combined->currentLossStreak;
        snprintf(streakBuf, sizeof(streakBuf), "%c%d (B:%d)",
                 currentStreakChar, currentStreakVal, combined->bestWinStreak);
        std::string streakStr = streakBuf;
        while (streakStr.length() < 10) streakStr += " ";

        table += "║ " + gameName + " ║ " + wl + " ║ " + winPctStr + " ║ " +
                 avgStr + " ║ " + bestStr + " ║ " + streakStr + " ║\n";
    }

    // Overall row
    table += "╠════════════════════════╬════════╬════════╬══════════╬══════════╬════════════╣\n";

    // Format overall stats
    std::string overallName = "OVERALL";
    while (overallName.length() < 22) overallName += " ";

    char wlBuf[16];
    snprintf(wlBuf, sizeof(wlBuf), "%2d/%2d", totalWins, totalLosses);
    std::string wl = wlBuf;
    while (wl.length() < 6) wl += " ";

    char winPctBuf[16];
    float totalPct = (totalWins + totalLosses > 0) ?
                     (100.0f * totalWins / (totalWins + totalLosses)) : 0.0f;
    snprintf(winPctBuf, sizeof(winPctBuf), "%5.1f%%", totalPct);

    GameStats overall = tracker.getOverallStats();
    char avgBuf[16];
    uint32_t overallAvg = (overall.totalAttempts > 0) ?
                          (overall.totalTimeMs / overall.totalAttempts) : 0;
    snprintf(avgBuf, sizeof(avgBuf), "%5.1fs", overallAvg / 1000.0f);
    std::string avgStr = avgBuf;
    while (avgStr.length() < 8) avgStr = " " + avgStr;

    char bestBuf[16];
    if (overallBestTime == UINT32_MAX) {
        snprintf(bestBuf, sizeof(bestBuf), "    --");
    } else {
        snprintf(bestBuf, sizeof(bestBuf), "%5.1fs", overallBestTime / 1000.0f);
    }
    std::string bestStr = bestBuf;
    while (bestStr.length() < 8) bestStr = " " + bestStr;

    char streakBuf[32];
    snprintf(streakBuf, sizeof(streakBuf), "%c%d (B:%d)",
             hasCurrentWinStreak ? 'W' : '-',
             maxCurrentStreak, maxBestStreak);
    std::string streakStr = streakBuf;
    while (streakStr.length() < 10) streakStr += " ";

    table += "║ " + overallName + " ║ " + wl + " ║ " + winPctBuf + " ║ " +
             avgStr + " ║ " + bestStr + " ║ " + streakStr + " ║\n";

    table += "╚════════════════════════╩════════╩════════╩══════════╩══════════╩════════════╝\n";

    result.message = "Device " + std::to_string(deviceIndex) + " (" + dev.deviceId + ") FDN Game Stats:" + table;
    return result;
}

CommandResult StatsCommands::cmdStatsDetailed(DeviceInstance& dev, int deviceIndex) {
    CommandResult result;
    Player* p = dev.player;
    const GameStatsTracker& tracker = p->getGameStatsTracker();

    if (!tracker.hasAnyGamesPlayed()) {
        result.message = "No FDN games played yet. Connect to an FDN device to start playing!";
        return result;
    }

    std::string output = "Device " + std::to_string(deviceIndex) + " (" + dev.deviceId + ") Detailed FDN Stats:\n\n";

    const GameType games[] = {
        GameType::GHOST_RUNNER, GameType::SPIKE_VECTOR, GameType::FIREWALL_DECRYPT,
        GameType::CIPHER_PATH, GameType::EXPLOIT_SEQUENCER, GameType::BREACH_DEFENSE,
        GameType::SIGNAL_ECHO
    };

    for (const GameType& game : games) {
        const PerGameStats* perGame = tracker.getPerGameStats(game);
        if (!perGame || perGame->combined.totalAttempts == 0) continue;

        output += "┌─ " + std::string(getGameDisplayName(game)) + " ─────────────\n";

        // Easy mode
        if (perGame->easy.totalAttempts > 0) {
            char buf[256];
            float easyWinRate = 100.0f * perGame->easy.wins / perGame->easy.totalAttempts;
            uint32_t easyAvg = perGame->easy.totalTimeMs / perGame->easy.totalAttempts;
            uint32_t easyBest = perGame->easy.bestTimeMs;

            snprintf(buf, sizeof(buf),
                     "│ EASY:  W:%d L:%d (%5.1f%%) | Avg:%.1fs Best:%s | Streak:W%d (Best:%d)\n",
                     perGame->easy.wins, perGame->easy.losses, easyWinRate,
                     easyAvg / 1000.0f,
                     (easyBest == UINT32_MAX) ? "--" : (std::to_string(easyBest/1000) + "." + std::to_string((easyBest%1000)/100) + "s").c_str(),
                     perGame->easy.currentWinStreak, perGame->easy.bestWinStreak);
            output += buf;
        }

        // Hard mode
        if (perGame->hard.totalAttempts > 0) {
            char buf[256];
            float hardWinRate = 100.0f * perGame->hard.wins / perGame->hard.totalAttempts;
            uint32_t hardAvg = perGame->hard.totalTimeMs / perGame->hard.totalAttempts;
            uint32_t hardBest = perGame->hard.bestTimeMs;

            snprintf(buf, sizeof(buf),
                     "│ HARD:  W:%d L:%d (%5.1f%%) | Avg:%.1fs Best:%s | Streak:W%d (Best:%d)\n",
                     perGame->hard.wins, perGame->hard.losses, hardWinRate,
                     hardAvg / 1000.0f,
                     (hardBest == UINT32_MAX) ? "--" : (std::to_string(hardBest/1000) + "." + std::to_string((hardBest%1000)/100) + "s").c_str(),
                     perGame->hard.currentWinStreak, perGame->hard.bestWinStreak);
            output += buf;
        }

        output += "└────────────────────────\n\n";
    }

    result.message = output;
    return result;
}

CommandResult StatsCommands::cmdStatsStreaks(DeviceInstance& dev, int deviceIndex) {
    CommandResult result;
    Player* p = dev.player;
    const GameStatsTracker& tracker = p->getGameStatsTracker();

    if (!tracker.hasAnyGamesPlayed()) {
        result.message = "No FDN games played yet. Connect to an FDN device to start playing!";
        return result;
    }

    std::string output = "Device " + std::to_string(deviceIndex) + " (" + dev.deviceId + ") Streak Stats:\n\n";

    const GameType games[] = {
        GameType::GHOST_RUNNER, GameType::SPIKE_VECTOR, GameType::FIREWALL_DECRYPT,
        GameType::CIPHER_PATH, GameType::EXPLOIT_SEQUENCER, GameType::BREACH_DEFENSE,
        GameType::SIGNAL_ECHO
    };

    for (const GameType& game : games) {
        const GameStats* combined = tracker.getCombinedStats(game);
        if (!combined || combined->totalAttempts == 0) continue;

        char buf[128];
        char currentChar = (combined->currentWinStreak > 0) ? 'W' : 'L';
        uint16_t currentVal = (combined->currentWinStreak > 0) ?
                              combined->currentWinStreak : combined->currentLossStreak;

        snprintf(buf, sizeof(buf), "%-22s Current: %c%-3d | Best Win: %-3d\n",
                 getGameDisplayName(game), currentChar, currentVal, combined->bestWinStreak);
        output += buf;
    }

    result.message = output;
    return result;
}

CommandResult StatsCommands::cmdStatsReset(const std::vector<std::string>& tokens,
                                           std::vector<DeviceInstance>& devices,
                                           int selectedDevice) {
    CommandResult result;

    // Determine target device
    int targetDevice = selectedDevice;
    if (tokens.size() >= 3) {
        targetDevice = CommandUtils::findDevice(tokens[2], devices, -1);
    }

    // Validate target device
    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device";
        return result;
    }

    auto& dev = devices[targetDevice];

    // Check if device has a player
    if (!dev.player) {
        result.message = "NPC device - no player stats";
        return result;
    }

    // Reset all game statistics
    dev.player->getGameStatsTracker().resetAll();
    result.message = "All FDN game statistics reset for device " + dev.deviceId;
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
