#ifdef NATIVE_BUILD

#include "cli/commands/duel-commands.hpp"
#include "cli/commands/command-utils.hpp"
#include "cli/cli-serial-broker.hpp"
#include "game/quickdraw-state-ids.hpp"
#include "state/state-machine.hpp"
#include "device/pdn.hpp"

namespace cli {

CommandResult DuelCommands::cmdDuel(const std::vector<std::string>& tokens,
                                    std::vector<DeviceInstance>& devices,
                                    int selectedDevice) {
    CommandResult result;

    // Syntax: duel <subcommand> [args]
    // Uses currently selected device by default

    // Determine target device (always use selected device)
    int targetDevice = selectedDevice;

    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device selection";
        return result;
    }

    auto& dev = devices[targetDevice];

    // No subcommand - show help
    if (tokens.size() < 2) {
        result.message = "Duel commands:\n"
                         "  duel history [count]    - Show last N duels (default 10)\n"
                         "  duel record             - Show overall W/L record\n"
                         "  duel record <opponent>  - Show record vs specific opponent\n"
                         "  duel series             - Show current best-of-3 series state\n"
                         "  rematch or r            - Rematch last opponent (if connected)\n\n"
                         "Note: Commands use the currently selected device (use LEFT/RIGHT arrows or 'select' command)";
        return result;
    }

    // Parse subcommand
    std::string subcommand = tokens[1];
    for (char& c : subcommand) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }

    // duel history [count]
    if (subcommand == "history" || subcommand == "h") {
        int count = 10;
        if (tokens.size() > 2) {
            count = std::atoi(tokens[2].c_str());
            if (count < 1) count = 1;
            if (count > 50) count = 50;
        }

        result.message = dev.deviceId + " - " + dev.duelHistory.getFormattedHistory(count);
        return result;
    }

    // duel record [opponent]
    if (subcommand == "record" || subcommand == "rec") {
        if (tokens.size() > 2) {
            // Show record vs specific opponent
            std::string opponent = tokens[2];
            result.message = dev.deviceId + " - " + dev.duelHistory.getVsRecord(opponent);
        } else {
            // Show overall record
            result.message = dev.deviceId + " - " + dev.duelHistory.getRecordSummary();
        }
        return result;
    }

    // duel series
    if (subcommand == "series" || subcommand == "s") {
        result.message = dev.deviceId + " - " + dev.seriesState.getSeriesSummary();
        return result;
    }

    result.message = "Unknown duel subcommand: " + subcommand + " (try 'duel' for help)";
    return result;
}

CommandResult DuelCommands::cmdRematch(const std::vector<std::string>& tokens,
                                       std::vector<DeviceInstance>& devices,
                                       int selectedDevice) {
    CommandResult result;

    // Use currently selected device
    int targetDevice = selectedDevice;

    if (targetDevice < 0 || targetDevice >= static_cast<int>(devices.size())) {
        result.message = "Invalid device selection";
        return result;
    }

    auto& dev = devices[targetDevice];

    // Check if device has a player (FDN devices can't duel)
    if (!dev.player) {
        result.message = "NPC device cannot rematch";
        return result;
    }

    // Check if we have duel history
    if (dev.duelHistory.getRecords().empty()) {
        result.message = "No duel history - nothing to rematch";
        return result;
    }

    // Get last opponent
    std::string lastOpponent = dev.duelHistory.getLastOpponent();
    int opponentIdx = CommandUtils::findDevice(lastOpponent, devices, -1);

    if (opponentIdx < 0) {
        result.message = "Last opponent (" + lastOpponent + ") not found";
        return result;
    }

    // Check if still connected via cable
    int connectedDevice = SerialCableBroker::getInstance().getConnectedDevice(targetDevice);
    if (connectedDevice != opponentIdx) {
        result.message = "Not connected to last opponent (" + lastOpponent + ")";
        return result;
    }

    // Trigger rematch by setting flag and transitioning to handshake
    dev.rematchPending = true;

    // If in a terminal state (Win/Lose), skip back to Idle then trigger handshake
    State* currentState = dev.game->getCurrentState();
    int stateId = currentState ? currentState->getStateId() : -1;

    if (stateId == QuickdrawStateId::WIN || stateId == QuickdrawStateId::LOSE) {
        // Skip to Idle state
        dev.game->skipToState(dev.pdn, QuickdrawStateId::IDLE);
    }

    // Now trigger handshake initiate (same as if button was pressed in Idle)
    // State 9 = HANDSHAKE_INITIATE_STATE
    if (dev.game->getCurrentState()->getStateId() == QuickdrawStateId::IDLE) {
        dev.game->skipToState(dev.pdn, QuickdrawStateId::HANDSHAKE_INITIATE_STATE);
    }

    result.message = "Rematch initiated with " + lastOpponent;
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
