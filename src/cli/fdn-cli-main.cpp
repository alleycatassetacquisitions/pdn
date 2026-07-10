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

// Platform abstractions
#include "utils/simple-timer.hpp"
#include "id-generator.hpp"
#include "state/state.hpp"

// CLI modules
#include "cli/cli-terminal.hpp"
#include "cli/cli-fdn-device.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-fdn-renderer.hpp"
#include "cli/cli-commands.hpp"

// Native drivers for global instances
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

// ============================================================================
// Globals
// ============================================================================

std::atomic<bool> g_running{true};

std::string g_commandBuffer;
std::string g_commandResult;

// selectedEntity: -1 = FDN, >=0 = PDN at that index
int g_selectedEntity = -1;

void signalHandler(int /*signal*/) { g_running = false; }

// ============================================================================
// FDN-specific command handling
// ============================================================================

/**
 * Parse a PDN index from a string (device ID or 0-based index).
 * Returns -1 if not found.
 */
static int findPDN(const std::string& target,
                   const std::vector<cli::DeviceInstance>& pdns) {
    for (int i = 0; i < static_cast<int>(pdns.size()); ++i) {
        if (pdns[i].deviceId == target || std::to_string(i) == target)
            return i;
    }
    return -1;
}

/**
 * Try to handle a command as an FDN-specific command.
 * Returns true and fills `result` if the command was handled.
 */
static bool handleFDNCommand(const std::string& input,
                             cli::FDNInstance& fdn,
                             std::vector<cli::DeviceInstance>& pdns,
                             int& selectedEntity,
                             cli::FDNRenderer& renderer,
                             std::string& result) {
    // Tokenise
    std::vector<std::string> tokens;
    std::string tok;
    for (char c : input) {
        if (c == ' ') { if (!tok.empty()) { tokens.push_back(tok); tok.clear(); } }
        else tok += c;
    }
    if (!tok.empty()) tokens.push_back(tok);
    if (tokens.empty()) return false;

    const std::string& cmd = tokens[0];

    // ---- FDN button presses ----
    if (cmd == "fb1" || cmd == "fb" || cmd == "fdn-button") {
        fdn.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        result = "FDN primary button click";
        return true;
    }
    if (cmd == "fb2" || cmd == "fdn-button2") {
        fdn.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        result = "FDN secondary button click";
        return true;
    }
    if (cmd == "fb3" || cmd == "fdn-button3") {
        fdn.tertiaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        result = "FDN tertiary button click";
        return true;
    }
    if (cmd == "fbl1" || cmd == "fdn-long") {
        fdn.primaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
        result = "FDN primary long press";
        return true;
    }
    if (cmd == "fbl2" || cmd == "fdn-long2") {
        fdn.secondaryButtonDriver->execCallback(ButtonInteraction::LONG_PRESS);
        result = "FDN secondary long press";
        return true;
    }

    // ---- FDN state ----
    if (cmd == "fdn-state" || cmd == "fs") {
        ::State* cur = fdn.demoModule ? fdn.demoModule->getCurrentState() : nullptr;
        int sid = cur ? cur->getStateId() : -1;
        result = std::string("FDN DemoModule state: [") + std::to_string(sid) + "] "
               + cli::getFDNStateName(sid);
        return true;
    }

    // ---- Connect PDN to FDN ----
    // cable-fdn <pdn_idx> [2]   -- jack 2 optional flag
    if (cmd == "cable-fdn" || cmd == "cfdn") {
        if (tokens.size() < 2) {
            result = "Usage: cable-fdn <pdn> [2]  -- '2' uses secondary FDN jack";
            return true;
        }
        int pdnIdx = findPDN(tokens[1], pdns);
        if (pdnIdx < 0) {
            result = "PDN not found: " + tokens[1];
            return true;
        }
        bool useJack2 = (tokens.size() >= 3 && tokens[2] == "2");

        if (cli::SerialCableBroker::getInstance().connectPdnToFdn(pdnIdx, fdn.fdnIndex, useJack2)) {
            result = std::string("Connected PDN ") + pdns[pdnIdx].deviceId
                   + " -> FDN " + fdn.fdnId
                   + (useJack2 ? " (jack 2)" : " (jack 1)")
                   + " -- handshake will complete in ~2s";
        } else {
            result = "Failed to connect (already connected or invalid device)";
        }
        return true;
    }

    // ---- Disconnect PDN from FDN ----
    // cable-fdn -d <pdn_idx>
    if ((cmd == "cable-fdn" || cmd == "cfdn") && tokens.size() >= 3 && tokens[1] == "-d") {
        int pdnIdx = findPDN(tokens[2], pdns);
        if (pdnIdx < 0) {
            result = "PDN not found: " + tokens[2];
            return true;
        }
        if (cli::SerialCableBroker::getInstance().disconnectPdnFromFdn(pdnIdx, fdn.fdnIndex)) {
            result = "Disconnected PDN " + pdns[pdnIdx].deviceId + " from FDN";
        } else {
            result = "Not connected";
        }
        return true;
    }

    // ---- Add a new PDN to the simulation ----
    // (same as existing 'add' command but needs MAX check from FDN simulator context)
    // Handled by standard CommandProcessor below.

    // ---- Select FDN ----
    if (cmd == "sel-fdn" || cmd == "sf") {
        selectedEntity = -1;
        result = "Selected FDN " + fdn.fdnId;
        return true;
    }

    // ---- Display / mirror / captions — forward to the real FDNRenderer ----
    if (cmd == "display" || cmd == "d") {
        if (tokens.size() >= 2) {
            std::string arg = tokens[1];
            for (char& c : arg) if (c >= 'A' && c <= 'Z') c += 32;
            if (arg == "on") {
                renderer.setDisplayMirror(true);
                renderer.setCaptions(true);
                result = "Display ON";
            } else if (arg == "off") {
                renderer.setDisplayMirror(false);
                renderer.setCaptions(false);
                result = "Display OFF";
            } else {
                result = "Usage: display [on|off]";
            }
        } else {
            if (renderer.isDisplayMirrorEnabled() != renderer.isCaptionsEnabled()) {
                renderer.setDisplayMirror(true);
                renderer.setCaptions(true);
            } else {
                renderer.toggleDisplayMirror();
                renderer.toggleCaptions();
            }
            result = std::string("Display ") + (renderer.isDisplayMirrorEnabled() ? "ON" : "OFF");
        }
        return true;
    }
    if (cmd == "mirror" || cmd == "m") {
        renderer.toggleDisplayMirror();
        result = std::string("Mirror ") + (renderer.isDisplayMirrorEnabled() ? "ON" : "OFF");
        return true;
    }
    if (cmd == "captions" || cmd == "cap") {
        renderer.toggleCaptions();
        result = std::string("Captions ") + (renderer.isCaptionsEnabled() ? "ON" : "OFF");
        return true;
    }

    // ---- Help ----
    if (cmd == "help" || cmd == "h" || cmd == "?") {
        result = "FDN: fb1/fb2/fb3=buttons  fbl1/fbl2=long-press  fs=fdn-state  "
                 "cable-fdn <n> [2]=connect PDN  sel-fdn=select FDN  "
                 "display/mirror/captions=UI  | PDN: b/b2/l/l2=buttons  state  peer  q=quit";
        return true;
    }

    return false;
}

// ============================================================================
// Header banner
// ============================================================================

static void printFDNHeader() {
    printf("\033[1;35m"); // Bold magenta for FDN
    printf("╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                              ║\n");
    printf("║         ███████╗██████╗ ███╗   ██╗    ███████╗██╗███╗   ███╗                ║\n");
    printf("║         ██╔════╝██╔══██╗████╗  ██║    ██╔════╝██║████╗ ████║                ║\n");
    printf("║         █████╗  ██║  ██║██╔██╗ ██║    ███████╗██║██╔████╔██║                ║\n");
    printf("║         ██╔══╝  ██║  ██║██║╚██╗██║    ╚════██║██║██║╚██╔╝██║                ║\n");
    printf("║         ██║     ██████╔╝██║ ╚████║    ███████║██║██║ ╚═╝ ██║                ║\n");
    printf("║         ╚═╝     ╚═════╝ ╚═╝  ╚═══╝    ╚══════╝╚═╝╚═╝     ╚═╝                ║\n");
    printf("║                                                                              ║\n");
    printf("║                     FDN Experience Simulator                                 ║\n");
    printf("║                                                                              ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════════════╝\n");
    printf("\033[0m");
    fflush(stdout);
}

// ============================================================================
// Setup helpers
// ============================================================================

static int promptPDNCount() {
    printf("\n\033[1;33m");
    printf("How many PDN controllers to simulate? (0-2, default 0)\n");
    printf("\033[0m");
    printf("\033[1;36mEnter count [0]: \033[0m");
    fflush(stdout);

    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return 0;
    int n = std::atoi(input.c_str());
    return (n >= 0 && n <= 2) ? n : 0;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    // Global platform setup
    auto* globalClock  = new NativeClockDriver("global_clock");
    auto* globalLogger = new NativeLoggerDriver("global_logger");
    globalLogger->setSuppressOutput(getenv("PDN_CLI_LOG_FILE") == nullptr);
    g_logger = globalLogger;
    SimpleTimer::setPlatformClock(globalClock);
    IdGenerator::initialize(globalClock->milliseconds());

    cli::Terminal::clearScreen();
    printFDNHeader();

    // Determine PDN count
    int pdnCount = 0;
    for (int i = 1; i < argc; ++i) {
        int n = std::atoi(argv[i]);
        if (n >= 0 && n <= 2) { pdnCount = n; break; }
        if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
            printf("FDN CLI Simulator\n");
            printf("Usage: %s [pdn_count (0-2)]\n", argv[0]);
            printf("  PDN count: number of simulated controller PDNs (default 0)\n");
            printf("  Commands: fb1/fb2/fb3=FDN buttons, cable-fdn <n>=connect PDN\n");
            return 0;
        }
    }
    if (argc == 1) pdnCount = promptPDNCount();

    printf("\n\033[1;32mCreating FDN + %d PDN(s)...\033[0m\n", pdnCount);

    // Create FDN
    cli::FDNInstance fdn = cli::FDNFactory::createFDN(0);
    printf("  FDN: %s (DemoModule)\n", fdn.fdnId.c_str());

    // Create PDN devices
    std::vector<cli::DeviceInstance> pdns;
    for (int i = 0; i < pdnCount; ++i) {
        pdns.push_back(cli::DeviceFactory::createDevice(i, /*isHunter=*/true));
        printf("  PDN %d: %s\n", i, pdns.back().deviceId.c_str());
    }

    printf("\nPress any key to start...\n");
    fflush(stdout);
    getchar();

    // Switch to raw terminal mode
    struct termios oldTermios = cli::Terminal::enableRawMode();

    cli::Terminal::clearScreen();
    cli::Terminal::hideCursor();
    printFDNHeader();

    cli::FDNRenderer renderer;
    cli::CommandProcessor pdnCommandProcessor;
    cli::Renderer pdnCmdRenderer;  // used only by mirror/captions/display sub-commands

    g_commandResult = "Ready! ↑↓=buttons, ENTER=FDN tertiary, LEFT/RIGHT=select, 'help' for cmds";

    while (g_running) {
        // ---- Input handling ----
        int key = cli::Terminal::readKey();
        while (key != static_cast<int>(cli::Key::NONE)) {
            if (key == static_cast<int>(cli::Key::ARROW_UP)) {
                // Primary button on selected entity
                if (g_selectedEntity == -1) {
                    fdn.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "FDN primary button click";
                } else if (g_selectedEntity < static_cast<int>(pdns.size())) {
                    pdns[g_selectedEntity].primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "PDN " + pdns[g_selectedEntity].deviceId + " primary click";
                }
            } else if (key == static_cast<int>(cli::Key::ARROW_DOWN)) {
                // Secondary button on selected entity
                if (g_selectedEntity == -1) {
                    fdn.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "FDN secondary button click";
                } else if (g_selectedEntity < static_cast<int>(pdns.size())) {
                    pdns[g_selectedEntity].secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "PDN " + pdns[g_selectedEntity].deviceId + " secondary click";
                }
            } else if (key == static_cast<int>(cli::Key::ARROW_LEFT)) {
                // Move selection left (FDN = -1, PDN[0], PDN[1], ...)
                if (g_selectedEntity > -1) {
                    g_selectedEntity--;
                    if (g_selectedEntity == -1)
                        g_commandResult = "Selected FDN " + fdn.fdnId;
                    else
                        g_commandResult = "Selected PDN " + pdns[g_selectedEntity].deviceId;
                }
            } else if (key == static_cast<int>(cli::Key::ARROW_RIGHT)) {
                // Move selection right
                if (g_selectedEntity < static_cast<int>(pdns.size()) - 1) {
                    g_selectedEntity++;
                    if (g_selectedEntity == -1)
                        g_commandResult = "Selected FDN " + fdn.fdnId;
                    else
                        g_commandResult = "Selected PDN " + pdns[g_selectedEntity].deviceId;
                }
            } else if (key == '\n' || key == '\r') {
                if (!g_commandBuffer.empty()) {
                    // Try FDN-specific commands first
                    if (!handleFDNCommand(g_commandBuffer, fdn, pdns, g_selectedEntity, renderer, g_commandResult)) {
                        // Fall back to standard PDN commands
                        // When FDN is selected (entity == -1), pass 0 so PDN commands
                        // default to the first PDN. Most commands accept an explicit target.
                        int pdnSel = (g_selectedEntity >= 0) ? g_selectedEntity : 0;
                        auto result = pdnCommandProcessor.execute(
                            g_commandBuffer, pdns, pdnSel, pdnCmdRenderer);
                        g_commandResult = result.message;
                        if (result.shouldQuit) g_running = false;
                    }
                    g_commandBuffer.clear();
                } else {
                    // Enter with empty buffer = FDN tertiary button
                    fdn.tertiaryButtonDriver->execCallback(ButtonInteraction::CLICK);
                    g_commandResult = "FDN tertiary button click";
                }
            } else if (key == 127 || key == '\b') {
                if (!g_commandBuffer.empty()) g_commandBuffer.pop_back();
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

        // ---- Simulation tick ----
        NativePeerBroker::getInstance().deliverPackets();
        cli::SerialCableBroker::getInstance().transferData();

        fdn.fdn->loop();
        for (auto& pdn : pdns) pdn.pdn->loop();

        // ---- Render ----
        renderer.renderUI(fdn, pdns, g_commandResult, g_commandBuffer, g_selectedEntity);

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    cli::Terminal::showCursor();
    cli::Terminal::restoreTerminal(oldTermios);
    printf("\n\nShutting down...\n");

    for (auto& pdn : pdns) cli::DeviceFactory::destroyDevice(pdn);
    cli::FDNFactory::destroyFDN(fdn);

    delete globalLogger;
    delete globalClock;

    return 0;
}

#endif // NATIVE_BUILD
