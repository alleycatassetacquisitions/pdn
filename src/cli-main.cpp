#ifdef NATIVE_BUILD

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <fstream>

// Platform abstraction
#include "utils/simple-timer.hpp"
#include "id-generator.hpp"

// CLI modules
#include "cli/cli-terminal.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-commands.hpp"

// Native drivers for global instances
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

// Constants
static constexpr int MIN_DEVICES = 1;
static constexpr int MAX_DEVICES = 8;

// Global running flag for signal handling
std::atomic<bool> g_running{true};

// Terminal resize flag for SIGWINCH handling
static volatile sig_atomic_t g_terminalResized = 0;

// Command input state
std::string g_commandBuffer;
std::string g_commandResult;
int g_selectedDevice = 0;  // Currently selected device index
std::string g_scriptFile;  // Script file path (empty = interactive mode)
std::vector<std::string> g_npcGames;  // NPC games to spawn (--npc flag)
std::string g_launchGame;  // Game to launch directly (--game flag)
bool g_autoCable = false;  // Auto-cable first player to first NPC (--auto-cable flag)

void signalHandler(int signal) {
    (void)signal;  // Suppress unused parameter warning
    g_running = false;
}

void sigwinchHandler(int) {
    g_terminalResized = 1;
}

/**
 * Parse command line arguments for device count and script file.
 * Sets g_scriptFile if --script flag is present.
 * Sets g_npcGames if --npc flags are present.
 * Sets g_launchGame if --game flag is present.
 * Sets g_autoCable if --auto-cable flag is present.
 * Returns -1 if no valid count specified (prompt needed).
 */
int parseArgs(int argc, char** argv) {
    int deviceCount = -1;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        // Script flag
        if (arg == "--script" && i + 1 < argc) {
            g_scriptFile = argv[++i];
            continue;
        }

        // NPC flag (can be repeated)
        if (arg == "--npc" && i + 1 < argc) {
            g_npcGames.push_back(argv[++i]);
            continue;
        }

        // Game flag (direct launch)
        if (arg == "--game" && i + 1 < argc) {
            g_launchGame = argv[++i];
            continue;
        }

        // Auto-cable flag
        if (arg == "--auto-cable") {
            g_autoCable = true;
            continue;
        }

        // Check for -n or --count flag
        if ((arg == "-n" || arg == "--count") && i + 1 < argc) {
            int count = std::atoi(argv[i + 1]);
            if (count >= MIN_DEVICES && count <= MAX_DEVICES) {
                deviceCount = count;
                i++;
            }
            continue;
        }

        // Help flag
        if (arg == "-h" || arg == "--help") {
            printf("PDN CLI Simulator\n");
            printf("Usage: %s [options] [device_count]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -n, --count N       Create N devices (1-%d)\n", MAX_DEVICES);
            printf("  --script <file>     Run demo script (non-interactive)\n");
            printf("  --npc <game>        Spawn an NPC/FDN device (can be repeated)\n");
            printf("  --game <game>       Launch directly into a specific minigame\n");
            printf("  --auto-cable        Auto-cable first player to first NPC\n");
            printf("  -h, --help          Show this help message\n");
            printf("\nValid game names:\n");
            printf("  ghost-runner, spike-vector, firewall-decrypt, cipher-path,\n");
            printf("  exploit-sequencer, breach-defense, signal-echo\n");
            printf("\nExamples:\n");
            printf("  %s                     Interactive prompt for device count\n", argv[0]);
            printf("  %s 3                   Create 3 devices\n", argv[0]);
            printf("  %s 2 --script demos/fdn-quickstart.demo\n", argv[0]);
            printf("  %s --npc ghost-runner --auto-cable\n", argv[0]);
            printf("  %s --game signal-echo  Launch Signal Echo directly\n", argv[0]);
            exit(0);
        }

        // Check for bare number argument
        int count = std::atoi(arg.c_str());
        if (count >= MIN_DEVICES && count <= MAX_DEVICES) {
            deviceCount = count;
        }
    }
    return deviceCount;
}

/**
 * Prompt user for device count with validation.
 */
int promptDeviceCount() {
    printf("\n");
    printf("\033[1;33m");  // Bold yellow
    printf("How many PDN devices would you like to simulate? (1-%d)\n", MAX_DEVICES);
    printf("\033[0m");
    printf("  - Device 1 will be a Hunter\n");
    printf("  - Device 2 will be a Bounty\n");
    printf("  - Roles alternate: Hunter, Bounty, Hunter, Bounty...\n");
    printf("\n");
    
    while (true) {
        printf("\033[1;36m");  // Bold cyan
        printf("Enter device count [1]: ");
        printf("\033[0m");
        fflush(stdout);
        
        std::string input;
        std::getline(std::cin, input);
        
        // Default to 1 if empty
        if (input.empty()) {
            return 1;
        }
        
        // Parse input
        int count = std::atoi(input.c_str());
        if (count >= MIN_DEVICES && count <= MAX_DEVICES) {
            return count;
        }
        
        printf("\033[1;31m");  // Bold red
        printf("Invalid input. Please enter a number between %d and %d.\n", MIN_DEVICES, MAX_DEVICES);
        printf("\033[0m");
    }
}

/**
 * Create devices with alternating hunter/bounty roles.
 * Also creates NPC devices if --npc flags were specified.
 * Also creates game devices if --game flag was specified.
 */
std::vector<cli::DeviceInstance> createDevices(int count) {
    std::vector<cli::DeviceInstance> devices;
    int nextDeviceIndex = 0;

    // Calculate total device count for display
    int playerCount = count;
    int npcCount = g_npcGames.size();
    int totalCount = playerCount + npcCount;

    devices.reserve(totalCount);

    printf("\n\033[1;32m");  // Bold green
    printf("Creating %d device%s...\n", totalCount, totalCount == 1 ? "" : "s");
    printf("\033[0m");

    // Create player devices (or game devices if --game is set)
    for (int i = 0; i < count; i++) {
        if (!g_launchGame.empty()) {
            // Create game device (standalone minigame)
            devices.push_back(cli::DeviceFactory::createGameDevice(nextDeviceIndex++, g_launchGame));
            printf("  Device %s: %s (standalone)\n",
                   devices.back().deviceId.c_str(),
                   g_launchGame.c_str());
        } else {
            // Create normal player device
            bool isHunter = (i % 2 == 0);
            devices.push_back(cli::DeviceFactory::createDevice(nextDeviceIndex++, isHunter));
            printf("  Device %s: %s\n",
                   devices.back().deviceId.c_str(),
                   isHunter ? "Hunter" : "Bounty");
        }
    }

    // Create NPC devices if --npc flags were specified
    for (const auto& npcGame : g_npcGames) {
        GameType gameType;
        if (!parseGameName(npcGame, gameType)) {
            printf("\033[1;31m");  // Bold red
            printf("  Warning: Unknown game '%s', skipping NPC\n", npcGame.c_str());
            printf("\033[0m");
            continue;
        }
        auto npcDevice = cli::DeviceFactory::createFdnDevice(nextDeviceIndex++, gameType);
        devices.push_back(npcDevice);
        printf("  Device %s: NPC (%s)\n",
               devices.back().deviceId.c_str(),
               getGameDisplayName(gameType));
    }

    // Auto-cable if requested
    if (g_autoCable && playerCount > 0 && npcCount > 0) {
        int playerIdx = 0;  // First player device
        int npcIdx = playerCount;  // First NPC device (after all players)
        if (cli::SerialCableBroker::getInstance().connect(playerIdx, npcIdx)) {
            printf("\n\033[1;33m");  // Bold yellow
            printf("Auto-cabled device %s <-> device %s\n",
                   devices[playerIdx].deviceId.c_str(),
                   devices[npcIdx].deviceId.c_str());
            printf("\033[0m");
        }
    }

    if (g_scriptFile.empty()) {
        printf("\n");
        printf("Press any key to start simulation...\n");
        fflush(stdout);
        #ifndef _WIN32
        getchar();
        #else
        _getch();
        #endif
    } else {
        printf("\n");
    }

    return devices;
}

/**
 * Check if a duel just completed and record it in duel history.
 * Called from main loop to detect Win/Lose state entry.
 */
void checkAndRecordDuel(std::vector<cli::DeviceInstance>& devices) {
    for (auto& dev : devices) {
        // Only track for player devices
        if (!dev.player || dev.deviceType != DeviceType::PLAYER) continue;

        State* currentState = dev.game->getCurrentState();
        if (!currentState) continue;

        int stateId = currentState->getStateId();

        // Update state history to track transitions
        dev.updateStateHistory(stateId);

        // Check if we just entered Win or Lose state (transition from any other state)
        bool isInWinLose = (stateId == QuickdrawStateId::WIN || stateId == QuickdrawStateId::LOSE);
        bool wasInWinLose = (dev.stateHistory.size() >= 2 &&
                            (dev.stateHistory[dev.stateHistory.size() - 2] == QuickdrawStateId::WIN ||
                             dev.stateHistory[dev.stateHistory.size() - 2] == QuickdrawStateId::LOSE));

        // Reset the recording flag when leaving Win/Lose state
        if (!isInWinLose && dev.duelRecordedThisSession) {
            dev.duelRecordedThisSession = false;
        }

        // Only record if we transitioned TO win/lose AND haven't recorded this duel yet
        if (isInWinLose && !wasInWinLose && !dev.duelRecordedThisSession) {
            dev.duelRecordedThisSession = true;

            // Find the opponent (connected device)
            int connectedDeviceIdx = cli::SerialCableBroker::getInstance().getConnectedDevice(dev.deviceIndex);
            if (connectedDeviceIdx < 0 || connectedDeviceIdx >= static_cast<int>(devices.size())) {
                continue;  // No connected opponent
            }

            auto& opponent = devices[connectedDeviceIdx];

            // Don't record duels with FDN devices
            if (opponent.deviceType != DeviceType::PLAYER) continue;

            // Determine outcome
            bool won = (stateId == QuickdrawStateId::WIN);

            // Get reaction times from players
            unsigned long playerRT = dev.player->getLastReactionTime();
            unsigned long opponentRT = opponent.player ? opponent.player->getLastReactionTime() : 0;

            // Get current time from global clock
            unsigned long currentTime = 0;
            PlatformClock* clock = SimpleTimer::getPlatformClock();
            if (clock) {
                currentTime = clock->milliseconds() / 1000;
            }

            // Create duel record
            cli::DuelRecord record(
                opponent.deviceId,
                "Quickdraw Duel",
                won,
                0,  // durationMs (could calculate from match timestamps if needed)
                static_cast<uint32_t>(currentTime),
                static_cast<uint32_t>(playerRT),
                static_cast<uint32_t>(opponentRT)
            );

            // Record in duel history
            dev.duelHistory.recordDuel(record);

            // If in a series, record game
            if (dev.seriesState.active) {
                dev.seriesState.recordGame(record);

                // Check if series is complete
                if (dev.seriesState.isComplete()) {
                    // Series ended, reset state after a delay
                    // (In a real implementation, might want to show series summary)
                    dev.seriesState.reset();
                }
            }
        }
    }
}

/**
 * Run a demo script file in non-interactive mode.
 * Reads commands line-by-line, executes them, and prints state after each.
 * Supports: commands, "wait <seconds>", and "# comment" lines.
 */
void runScript(const std::string& path,
               std::vector<cli::DeviceInstance>& devices,
               cli::CommandProcessor& proc,
               cli::Renderer& renderer) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open script file: " << path << std::endl;
        return;
    }

    std::cout << "--- Running script: " << path << " ---" << std::endl;

    std::string line;
    while (std::getline(file, line) && g_running) {
        // Trim whitespace (including \r from Windows line endings)
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Handle "wait <seconds>"
        if (line.size() > 5 && line.substr(0, 5) == "wait ") {
            float secs = std::stof(line.substr(5));
            std::cout << "> wait " << secs << "s" << std::endl;
            auto end = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(static_cast<int>(secs * 1000));
            while (std::chrono::steady_clock::now() < end && g_running) {
                NativePeerBroker::getInstance().deliverPackets();
                cli::SerialCableBroker::getInstance().transferData();
                for (auto& d : devices) d.pdn->loop();
                checkAndRecordDuel(devices);
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
            continue;
        }

        // Execute command
        std::cout << "> " << line << std::endl;
        auto result = proc.execute(line, devices, g_selectedDevice, renderer);
        if (!result.message.empty()) {
            std::cout << result.message << std::endl;
        }

        // Print state after each command
        for (size_t i = 0; i < devices.size(); i++) {
            auto* sm = devices[i].game;
            if (sm && sm->getCurrentState()) {
                std::cout << "  [" << devices[i].deviceId << "] "
                          << cli::getStateName(sm->getCurrentState()->getStateId(),
                                               devices[i].deviceType)
                          << std::endl;
            }
        }

        if (result.shouldQuit) break;

        // One frame tick between commands
        NativePeerBroker::getInstance().deliverPackets();
        cli::SerialCableBroker::getInstance().transferData();
        for (auto& d : devices) d.pdn->loop();
        checkAndRecordDuel(devices);
    }

    std::cout << "--- Script complete ---" << std::endl;
}

int main(int argc, char** argv) {
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Set up SIGWINCH handler for terminal resize (POSIX only)
    #ifndef _WIN32
    struct sigaction sa;
    sa.sa_handler = sigwinchHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);
    #endif
    
    // Create global clock and logger
    NativeClockDriver* globalClock = new NativeClockDriver("global_clock");
    NativeLoggerDriver* globalLogger = new NativeLoggerDriver("global_logger");
    
    // Suppress logger output during prompt phase
    globalLogger->setSuppressOutput(true);
    
    // Set up global platform abstractions
    g_logger = globalLogger;
    SimpleTimer::setPlatformClock(globalClock);
    
    // Seed the ID generator
    IdGenerator(globalClock->milliseconds()).seed(globalClock->milliseconds());
    
    // Parse arguments first (sets g_scriptFile and device count)
    int deviceCount = parseArgs(argc, argv);

    // Show header (skip in script mode)
    if (g_scriptFile.empty()) {
        cli::Terminal::clearScreen();
        cli::printHeader();
    }

    // Determine device count
    if (deviceCount < 0) {
        if (!g_scriptFile.empty()) {
            deviceCount = 2;  // Default for script mode
        } else {
            deviceCount = promptDeviceCount();
        }
    }

    // Create devices
    std::vector<cli::DeviceInstance> devices = createDevices(deviceCount);

    // Create CLI components
    cli::Renderer renderer;
    cli::CommandProcessor commandProcessor;

    if (!g_scriptFile.empty()) {
        // Script mode — no raw terminal, no TUI
        runScript(g_scriptFile, devices, commandProcessor, renderer);
    } else {
        // Interactive mode
        #ifndef _WIN32
        struct termios oldTermios = cli::Terminal::enableRawMode();
        #endif

        cli::Terminal::clearScreen();
        cli::Terminal::hideCursor();
        cli::printHeader();

        g_commandResult = "Ready! Use LEFT/RIGHT to select device, UP/DOWN for buttons. Type 'help' for commands.";

        // Main loop
        while (g_running) {
            // Handle terminal resize
            if (g_terminalResized) {
                g_terminalResized = 0;
                renderer.handleResize();
            }

            // Handle input (non-blocking)
            int key = cli::Terminal::readKey();
            while (key != static_cast<int>(cli::Key::NONE)) {
                if (key == static_cast<int>(cli::Key::ARROW_UP)) {
                    if (g_selectedDevice >= 0 && g_selectedDevice < static_cast<int>(devices.size())) {
                        devices[g_selectedDevice].primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                        g_commandResult = "Button1 click on " + devices[g_selectedDevice].deviceId;
                    }
                } else if (key == static_cast<int>(cli::Key::ARROW_DOWN)) {
                    if (g_selectedDevice >= 0 && g_selectedDevice < static_cast<int>(devices.size())) {
                        devices[g_selectedDevice].secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                        g_commandResult = "Button2 click on " + devices[g_selectedDevice].deviceId;
                    }
                } else if (key == static_cast<int>(cli::Key::ARROW_LEFT)) {
                    if (g_selectedDevice > 0) {
                        g_selectedDevice--;
                        g_commandResult = "Selected device " + devices[g_selectedDevice].deviceId;
                    }
                } else if (key == static_cast<int>(cli::Key::ARROW_RIGHT)) {
                    if (g_selectedDevice < static_cast<int>(devices.size()) - 1) {
                        g_selectedDevice++;
                        g_commandResult = "Selected device " + devices[g_selectedDevice].deviceId;
                    }
                } else if (key == '\n' || key == '\r') {
                    auto result = commandProcessor.execute(g_commandBuffer, devices, g_selectedDevice, renderer);
                    g_commandResult = result.message;
                    if (result.shouldQuit) {
                        g_running = false;
                    }
                    g_commandBuffer.clear();
                } else if (key == 127 || key == '\b') {
                    if (!g_commandBuffer.empty()) {
                        g_commandBuffer.pop_back();
                    }
                } else if (key == 27) {
                    g_commandBuffer.clear();
                    g_commandResult.clear();
                } else if (key == 3) {
                    g_running = false;
                } else if (key >= 32 && key < 127) {
                    g_commandBuffer += static_cast<char>(key);
                }
                key = cli::Terminal::readKey();
            }

            NativePeerBroker::getInstance().deliverPackets();
            cli::SerialCableBroker::getInstance().transferData();
            for (auto& device : devices) {
                device.pdn->loop();
            }

            // Check for completed duels and record them
            checkAndRecordDuel(devices);

            renderer.renderUI(devices, g_commandResult, g_commandBuffer, g_selectedDevice);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        cli::Terminal::showCursor();
        printf("\n\nShutting down...\n");
    }

    for (auto& device : devices) {
        cli::DeviceFactory::destroyDevice(device);
    }

    delete globalLogger;
    delete globalClock;

    return 0;
}

#endif // NATIVE_BUILD
