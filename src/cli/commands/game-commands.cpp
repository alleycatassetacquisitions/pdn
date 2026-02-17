#ifdef NATIVE_BUILD

#include "cli/commands/game-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "cli/cli-device.hpp"
#include "device/device-types.hpp"
#include "game/quickdraw.hpp"
#include "game/difficulty-scaler.hpp"
#include <cstdio>

namespace cli {

CommandResult GameCommands::cmdKonami(const std::vector<std::string>& tokens,
                                      std::vector<DeviceInstance>& devices,
                                      int selectedDevice) {
    CommandResult result;
    int targetDevice = selectedDevice;
    if (tokens.size() >= 3) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, selectedDevice);
    }
    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device";
        return result;
    }
    auto& dev = devices[targetDevice];
    if (!dev.player) {
        result.message = "Device has no player (FDN devices don't have players)";
        return result;
    }
    if (tokens.size() < 2) {
        // Show current progress
        uint8_t progress = dev.player->getKonamiProgress();
        bool boon = dev.player->hasKonamiBoon();
        char buf[64];
        snprintf(buf, sizeof(buf), "Konami: 0x%02X (%d/7) boon=%s",
                 progress, __builtin_popcount(progress & 0x7F),
                 boon ? "yes" : "no");
        result.message = buf;
        return result;
    }

    // Two-token case: konami <device> — show that device's progress
    if (tokens.size() == 2) {
        // Check if tokens[1] is a valid device ID or index
        bool found = false;
        int deviceIdx = -1;
        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i].deviceId == tokens[1] || std::to_string(i) == tokens[1]) {
                deviceIdx = static_cast<int>(i);
                found = true;
                break;
            }
        }

        if (found && deviceIdx >= 0) {
            auto& targetDev = devices[deviceIdx];
            if (!targetDev.player) {
                result.message = "Device has no player (FDN devices don't have players)";
                return result;
            }
            uint8_t progress = targetDev.player->getKonamiProgress();
            bool boon = targetDev.player->hasKonamiBoon();
            char buf[64];
            snprintf(buf, sizeof(buf), "Konami: 0x%02X (%d/7) boon=%s",
                     progress, __builtin_popcount(progress & 0x7F),
                     boon ? "yes" : "no");
            result.message = buf;
            return result;
        }
        result.message = "Unknown device: " + tokens[1];
        return result;
    }

    // Three+ tokens: konami set <device> <value>
    if (tokens.size() >= 3 && tokens[1] == "set") {
        int deviceIdx = CommandUtils::findDevice(tokens[2], devices, selectedDevice);
        if (deviceIdx < 0 || deviceIdx >= static_cast<int>(devices.size())) {
            result.message = "Invalid device";
            return result;
        }
        auto& targetDev = devices[deviceIdx];
        if (!targetDev.player) {
            result.message = "Device has no player (FDN devices don't have players)";
            return result;
        }
        int value = std::atoi(tokens[3].c_str());
        targetDev.player->setKonamiProgress(static_cast<uint8_t>(value & 0xFF));

        // Auto-boon if all 7 set
        if (targetDev.player->isKonamiComplete()) {
            targetDev.player->setKonamiBoon(true);
        }

        uint8_t progress = targetDev.player->getKonamiProgress();
        bool boon = targetDev.player->hasKonamiBoon();
        char buf[64];
        snprintf(buf, sizeof(buf), "Konami set: 0x%02X (%d/7) boon=%s",
                 progress, __builtin_popcount(progress & 0x7F),
                 boon ? "yes" : "no");
        result.message = buf;
        return result;
    }

    // Invalid command format
    result.message = "Usage: konami [<device>] OR konami set <device> <value>";
    return result;
}

CommandResult GameCommands::cmdGames(const std::vector<std::string>& /*tokens*/) {
    CommandResult result;
    result.message = "Available games:\n"
                     "  quickdraw         - Quickdraw Duel\n"
                     "  ghost-runner      - Ghost Runner\n"
                     "  spike-vector      - Spike Vector\n"
                     "  firewall-decrypt  - Firewall Decrypt\n"
                     "  cipher-path       - Cipher Path\n"
                     "  exploit-sequencer - Exploit Sequencer\n"
                     "  breach-defense    - Breach Defense\n"
                     "  signal-echo       - Signal Echo";
    return result;
}

CommandResult GameCommands::cmdProgress(const std::vector<std::string>& tokens,
                                        const std::vector<DeviceInstance>& devices,
                                        int selectedDevice) {
    CommandResult result;

    // Determine target device
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, -1);
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

    Player* p = dev.player;
    uint8_t progress = p->getKonamiProgress();
    bool boon = p->hasKonamiBoon();

    // Build visual grid
    std::string grid = dev.deviceId + " Konami Progress:\n\n";

    // Order: UP, DOWN, LEFT, RIGHT, B, A, START
    // Visual layout:
    //     [UP]
    // [L] [D] [R]
    //  [B] [A]
    //   [START]

    const char* buttonNames[7] = {"UP", "DOWN", "LEFT", "RIGHT", "B", "A", "START"};
    const char* gameNames[7] = {
        "Signal Echo",      // UP
        "Spike Vector",     // DOWN
        "Firewall Decrypt", // LEFT
        "Cipher Path",      // RIGHT
        "Exploit Seq",      // B
        "Breach Defense",   // A
        "Ghost Runner"      // START
    };

    // Build grid line by line
    //     [UP]
    bool hasUp = (progress & (1 << 0)) != 0;
    grid += "      [" + std::string(hasUp ? "X" : " ") + "] UP - " + gameNames[0] + "\n";

    // [L] [D] [R]
    bool hasLeft = (progress & (1 << 2)) != 0;
    bool hasDown = (progress & (1 << 1)) != 0;
    bool hasRight = (progress & (1 << 3)) != 0;
    grid += "  [" + std::string(hasLeft ? "X" : " ") + "] L   ";
    grid += "[" + std::string(hasDown ? "X" : " ") + "] D   ";
    grid += "[" + std::string(hasRight ? "X" : " ") + "] R\n";
    grid += "  " + std::string(gameNames[2]) + "  " + std::string(gameNames[1]) + "  " + std::string(gameNames[3]) + "\n";

    //  [B] [A]
    bool hasB = (progress & (1 << 4)) != 0;
    bool hasA = (progress & (1 << 5)) != 0;
    grid += "    [" + std::string(hasB ? "X" : " ") + "] B   ";
    grid += "[" + std::string(hasA ? "X" : " ") + "] A\n";
    grid += "    " + std::string(gameNames[4]) + "  " + std::string(gameNames[5]) + "\n";

    //   [START]
    bool hasStart = (progress & (1 << 6)) != 0;
    grid += "       [" + std::string(hasStart ? "X" : " ") + "] START\n";
    grid += "       " + std::string(gameNames[6]) + "\n";

    // Summary
    int count = __builtin_popcount(progress & 0x7F);
    grid += "\n" + std::to_string(count) + "/7 unlocked";
    if (boon) {
        grid += " | BOON ACTIVE";
    }

    result.message = grid;
    return result;
}

CommandResult GameCommands::cmdColors(const std::vector<std::string>& tokens,
                                      const std::vector<DeviceInstance>& devices,
                                      int selectedDevice) {
    CommandResult result;

    // Determine target device
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, -1);
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

    Player* p = dev.player;
    int equipped = p->getEquippedColorProfile();
    const std::set<int>& eligible = p->getColorProfileEligibility();

    // Build color profile status
    std::string output = dev.deviceId + " Color Profiles:\n\n";

    // Default profile (always available)
    std::string defaultName = dev.isHunter ? "HUNTER DEFAULT" : "BOUNTY DEFAULT";
    output += "[" + std::string(equipped == -1 ? "*" : " ") + "] " + defaultName;
    output += " (default)\n";

    // Game profiles
    const GameType games[7] = {
        GameType::GHOST_RUNNER,
        GameType::SPIKE_VECTOR,
        GameType::FIREWALL_DECRYPT,
        GameType::CIPHER_PATH,
        GameType::EXPLOIT_SEQUENCER,
        GameType::BREACH_DEFENSE,
        GameType::SIGNAL_ECHO
    };

    for (int i = 0; i < 7; i++) {
        GameType game = games[i];
        int gameValue = static_cast<int>(game);
        bool isEligible = eligible.count(gameValue) > 0;
        bool isEquipped = (equipped == gameValue);

        std::string status;
        if (isEquipped) {
            status = "[*] ";
        } else if (isEligible) {
            status = "[ ] ";
        } else {
            status = "[X] ";
        }

        output += status + getGameDisplayName(game);

        if (!isEligible) {
            output += " (locked - beat hard mode to unlock)";
        } else if (isEquipped) {
            output += " (equipped)";
        }
        output += "\n";
    }

    result.message = output;
    return result;
}

CommandResult GameCommands::cmdDifficulty(const std::vector<std::string>& tokens,
                                          const std::vector<DeviceInstance>& devices,
                                          int selectedDevice) {
    CommandResult result;

    // Handle "difficulty reset" sub-command
    if (tokens.size() >= 2 && tokens[1] == "reset") {
        // Determine target device (3rd token or selected)
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

        // Check if device has Quickdraw game
        if (dev.deviceType != DeviceType::PLAYER || !dev.game) {
            result.message = "Device does not have difficulty scaling";
            return result;
        }

        // Cast to Quickdraw and get the scaler
        auto* quickdraw = dynamic_cast<Quickdraw*>(dev.game);
        if (!quickdraw) {
            result.message = "Device does not have Quickdraw game";
            return result;
        }

        DifficultyScaler* scaler = quickdraw->getDifficultyScaler();
        if (!scaler) {
            result.message = "Difficulty scaler not available";
            return result;
        }

        scaler->resetAll();
        result.message = "Difficulty scaling reset for device " + dev.deviceId;
        return result;
    }

    // Default: show difficulty status for selected or specified device
    int targetDevice = selectedDevice;
    if (tokens.size() >= 2) {
        targetDevice = CommandUtils::findDevice(tokens[1], devices, -1);
    }

    // Validate target device
    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device";
        return result;
    }

    auto& dev = devices[targetDevice];

    // Check if device has Quickdraw game
    if (dev.deviceType != DeviceType::PLAYER || !dev.game) {
        result.message = "Device does not have difficulty scaling";
        return result;
    }

    // Cast to Quickdraw and get the scaler
    auto* quickdraw = dynamic_cast<Quickdraw*>(dev.game);
    if (!quickdraw) {
        result.message = "Device does not have Quickdraw game";
        return result;
    }

    DifficultyScaler* scaler = quickdraw->getDifficultyScaler();
    if (!scaler) {
        result.message = "Difficulty scaler not available";
        return result;
    }

    // Build difficulty status display
    std::string output = dev.deviceId + " Difficulty Auto-Scaling:\n\n";

    const char* gameNames[] = {
        "QUICKDRAW",
        "GHOST RUNNER",
        "SPIKE VECTOR",
        "FIREWALL DECRYPT",
        "CIPHER PATH",
        "EXPLOIT SEQUENCER",
        "BREACH DEFENSE",
        "SIGNAL ECHO"
    };

    // Display scaling for each minigame (skip QUICKDRAW at index 0)
    for (int i = 1; i < 8; i++) {
        GameType gameType = static_cast<GameType>(i);
        float scale = scaler->getScaledDifficulty(gameType);
        std::string label = scaler->getDifficultyLabel(gameType);
        PerformanceMetrics metrics = scaler->getMetrics(gameType);

        // Create visual bar (10 blocks)
        std::string bar;
        int filled = static_cast<int>(scale * 10);
        for (int j = 0; j < 10; j++) {
            bar += (j < filled) ? "\u2588" : "\u2591";
        }

        // Format: "Ghost Runner:     ████░░░░░░  0.42 (Easy+) | 5 played"
        char line[128];
        snprintf(line, sizeof(line), "%-17s %s  %.2f (%s) | %d played\n",
                 gameNames[i], bar.c_str(), scale, label.c_str(), metrics.totalPlayed);
        output += line;
    }

    output += "\nUse 'difficulty reset' to reset all scaling to 0.0";

    result.message = output;
    return result;
}

CommandResult GameCommands::cmdDemo(const std::vector<std::string>& tokens,
                                    std::vector<DeviceInstance>& devices,
                                    int& selectedDevice) {
    CommandResult result;

    // Show help if no arguments
    if (tokens.size() < 2) {
        result.message = "Demo Mode - Play FDN Minigames\n\n"
                       "Usage: demo <game> [easy|hard]\n"
                       "       demo list - show all games\n"
                       "       demo all [easy|hard] - play all 7 games\n\n"
                       "Available games:\n"
                       "  signal-echo       - Memory sequence game\n"
                       "  ghost-runner      - Timing/reaction game\n"
                       "  spike-vector      - Targeting game\n"
                       "  firewall-decrypt  - Pattern matching puzzle\n"
                       "  cipher-path       - Pathfinding puzzle\n"
                       "  exploit-sequencer - Quick-time events\n"
                       "  breach-defense    - Defense strategy\n\n"
                       "Difficulty: easy (default), hard\n"
                       "Example: demo signal-echo hard";
        return result;
    }

    std::string subcommand = tokens[1];
    for (char& c : subcommand) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }

    // List games
    if (subcommand == "list") {
        result.message = "Available FDN Demo Games:\n\n"
                       "1. Signal Echo       - Repeat the light sequence from memory\n"
                       "2. Ghost Runner      - Hit the ghost when it enters the target zone\n"
                       "3. Spike Vector      - Target the moving spike walls\n"
                       "4. Firewall Decrypt  - Match the decryption pattern\n"
                       "5. Cipher Path       - Navigate the cipher maze\n"
                       "6. Exploit Sequencer - Execute the exploit chain\n"
                       "7. Breach Defense    - Defend against incoming threats\n\n"
                       "Use 'demo <game> [easy|hard]' to play";
        return result;
    }

    // Determine difficulty
    std::string difficulty = "easy";
    if (tokens.size() >= 3) {
        difficulty = tokens[2];
        for (char& c : difficulty) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        if (difficulty != "easy" && difficulty != "hard") {
            result.message = "Invalid difficulty: " + tokens[2] + " (use 'easy' or 'hard')";
            return result;
        }
    }

    // Play all games
    if (subcommand == "all") {
        result.message = "Demo Mode: Playing all 7 games at " + difficulty + " difficulty\n"
                       "Use 'add challenge <game>' to create a device for each game";
        return result;
    }

    // Parse game name
    GameType gameType;
    if (!parseGameName(subcommand, gameType)) {
        result.message = "Invalid game: " + tokens[1] + " - try 'demo list'";
        return result;
    }

    // Check if a demo device already exists
    int demoDeviceIndex = -1;
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i].deviceId.find("DEMO-") == 0) {
            demoDeviceIndex = static_cast<int>(i);
            break;
        }
    }

    // Create or reuse demo device
    if (demoDeviceIndex < 0) {
        // Check max device limit
        static constexpr int MAX_DEVICES = 11;
        if (devices.size() >= MAX_DEVICES) {
            result.message = "Cannot create demo device (max " + std::to_string(MAX_DEVICES) + " devices)";
            return result;
        }

        // Create new challenge device
        int newIndex = static_cast<int>(devices.size());
        devices.push_back(DeviceFactory::createGameDevice(newIndex, subcommand));
        demoDeviceIndex = newIndex;
        selectedDevice = newIndex;

        // Rename to indicate demo mode
        devices[demoDeviceIndex].deviceId = "DEMO-" + std::to_string(newIndex);
    }

    result.message = std::string("Demo: ") + getGameDisplayName(gameType) + " (" + difficulty + ")\n" +
                   "Device " + devices[demoDeviceIndex].deviceId + " ready\n" +
                   "Press PRIMARY button to start the game\n" +
                   "Use 'b' or 'b2' commands to simulate button presses";

    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
