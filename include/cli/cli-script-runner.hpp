#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstdlib>

#include "cli/cli-device.hpp"
#include "cli/cli-commands.hpp"
#include "cli/cli-event-logger.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

namespace cli {

/**
 * A single parsed command from a script file.
 */
struct ScriptCommand {
    std::string command;
    int lineNumber;
};

/**
 * ScriptRunner executes CLI automation scripts line-by-line.
 *
 * Scripts contain CLI commands, timing directives (wait/tick),
 * button presses, and assertions. Comments start with '#'.
 *
 * Lifecycle:
 *   1. Construct with references to devices, processor, and event logger.
 *   2. Load a script via loadFromFile() or loadFromString().
 *   3. Execute with executeAll() (stops on first error) or
 *      step through with executeNext().
 */
class ScriptRunner {
public:
    ScriptRunner(std::vector<DeviceInstance>& devices,
                 CommandProcessor& commandProcessor,
                 EventLogger& eventLogger) :
        devices_(devices),
        commandProcessor_(commandProcessor),
        eventLogger_(eventLogger)
    {
    }

    /**
     * Load script commands from a file.
     * Returns false if the file cannot be opened.
     */
    bool loadFromFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        loadFromString(content);
        return true;
    }

    /**
     * Load script commands from a string.
     * Strips comments (everything after #), trims whitespace,
     * and skips blank lines.
     */
    void loadFromString(const std::string& script) {
        commands_.clear();
        currentIndex_ = 0;

        std::istringstream stream(script);
        std::string line;
        int lineNumber = 0;

        while (std::getline(stream, line)) {
            lineNumber++;

            // Strip comment (everything after #)
            size_t commentPos = line.find('#');
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }

            // Trim whitespace
            line = trim(line);

            // Skip empty lines
            if (line.empty()) {
                continue;
            }

            commands_.push_back({line, lineNumber});
        }
    }

    /**
     * Execute the next command in the script.
     * Returns empty string on success, or an error message on failure.
     */
    std::string executeNext() {
        if (!hasMore()) {
            return "No more commands";
        }

        const ScriptCommand& cmd = commands_[currentIndex_];
        currentIndex_++;

        std::string error = executeCommand(cmd);
        if (!error.empty()) {
            return "Line " + std::to_string(cmd.lineNumber) + ": " + error;
        }
        return "";
    }

    /**
     * Execute all remaining commands.
     * Stops on the first error and returns that error message.
     * Returns empty string if all commands succeeded.
     */
    std::string executeAll() {
        while (hasMore()) {
            std::string error = executeNext();
            if (!error.empty()) {
                return error;
            }
        }
        return "";
    }

    /**
     * Returns true if there are more commands to execute.
     */
    bool hasMore() const {
        return currentIndex_ < commands_.size();
    }

    /**
     * Returns the line number of the current (next-to-execute) command,
     * or -1 if no more commands remain.
     */
    int currentLine() const {
        if (currentIndex_ < commands_.size()) {
            return commands_[currentIndex_].lineNumber;
        }
        return -1;
    }

    /**
     * Returns the total number of parsed commands.
     */
    size_t commandCount() const {
        return commands_.size();
    }

private:
    std::vector<DeviceInstance>& devices_;
    CommandProcessor& commandProcessor_;
    EventLogger& eventLogger_;

    std::vector<ScriptCommand> commands_;
    size_t currentIndex_ = 0;

    // Dummy renderer for passing to CommandProcessor
    Renderer dummyRenderer_;
    int selectedDevice_ = 0;

    static constexpr unsigned long TICK_MS = 33;

    /**
     * Execute a single script command.
     * Returns empty string on success, error message on failure.
     */
    std::string executeCommand(const ScriptCommand& cmd) {
        std::vector<std::string> tokens = tokenize(cmd.command);
        if (tokens.empty()) {
            return "";
        }

        const std::string& verb = tokens[0];

        // Script-specific commands handled directly
        if (verb == "wait") {
            return handleWait(tokens);
        }
        if (verb == "tick") {
            return handleTick(tokens);
        }
        if (verb == "press") {
            return handlePress(tokens);
        }
        if (verb == "longpress") {
            return handleLongPress(tokens);
        }
        if (verb == "assert-state") {
            return handleAssertState(tokens);
        }
        if (verb == "assert-text") {
            return handleAssertText(tokens);
        }
        if (verb == "assert-serial-tx") {
            return handleAssertSerialTx(tokens);
        }
        if (verb == "assert-serial-rx") {
            return handleAssertSerialRx(tokens);
        }
        if (verb == "assert-event") {
            return handleAssertEvent(tokens);
        }
        if (verb == "assert-no-event") {
            return handleAssertNoEvent(tokens);
        }
        if (verb == "advance") {
            return handleAdvance(tokens);
        }
        if (verb == "inspect") {
            return handleInspect(tokens);
        }
        if (verb == "events") {
            return handleEvents(tokens);
        }
        if (verb == "clear-events") {
            eventLogger_.clear();
            return "";
        }

        // Everything else passes through to CommandProcessor
        CommandResult result = commandProcessor_.execute(
            cmd.command, devices_, selectedDevice_, dummyRenderer_);

        if (result.shouldQuit) {
            return "";  // Quit is valid, not an error
        }

        // CommandProcessor returns "Unknown command: ..." for bad commands
        if (result.message.find("Unknown command") != std::string::npos) {
            return result.message;
        }

        return "";
    }

    // ==================== COMMAND HANDLERS ====================

    /**
     * wait <ms> - Advance all device clocks by <ms> milliseconds
     * and run the appropriate number of loop iterations.
     */
    std::string handleWait(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return "Usage: wait <ms>";
        }

        long msRaw = std::atol(tokens[1].c_str());
        if (msRaw <= 0) {
            return "wait requires a positive millisecond value";
        }
        unsigned long ms = static_cast<unsigned long>(msRaw);

        // Interleave clock advances with loop iterations (same as tick)
        unsigned long iterations = ms / TICK_MS;
        if (iterations < 1) iterations = 1;
        unsigned long advanced = 0;

        for (unsigned long i = 0; i < iterations; i++) {
            unsigned long step = (i < iterations - 1) ? TICK_MS : (ms - advanced);
            for (auto& dev : devices_) {
                dev.clockDriver->advance(step);
            }
            advanced += step;
            runOneLoopIteration();
        }

        return "";
    }

    /**
     * tick [N] - Run N loop iterations (default 1).
     * Each iteration advances clocks by TICK_MS (33ms).
     */
    std::string handleTick(const std::vector<std::string>& tokens) {
        unsigned long count = 1;
        if (tokens.size() >= 2) {
            long raw = std::atol(tokens[1].c_str());
            count = (raw > 0) ? static_cast<unsigned long>(raw) : 1;
        }

        for (unsigned long i = 0; i < count; i++) {
            // Advance clocks by one tick
            for (auto& dev : devices_) {
                dev.clockDriver->advance(TICK_MS);
            }
            runOneLoopIteration();
        }

        return "";
    }

    /**
     * press <device> primary|secondary - Simulate a button click.
     */
    std::string handlePress(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            return "Usage: press <device> primary|secondary";
        }

        int devIdx = std::atoi(tokens[1].c_str());
        if (devIdx < 0 || devIdx >= static_cast<int>(devices_.size())) {
            return "Invalid device index: " + tokens[1];
        }

        std::string button = tokens[2];
        NativeButtonDriver* driver = nullptr;

        if (button == "primary") {
            driver = devices_[devIdx].primaryButtonDriver;
        } else if (button == "secondary") {
            driver = devices_[devIdx].secondaryButtonDriver;
        } else {
            return "Unknown button: " + button + " (use primary or secondary)";
        }

        driver->execCallback(ButtonInteraction::CLICK);

        // Log a BUTTON_PRESS event
        Event evt;
        evt.timestampMs = devices_[devIdx].clockDriver->milliseconds();
        evt.type = EventType::BUTTON_PRESS;
        evt.deviceIndex = devIdx;
        evt.button = button;
        evt.detail = "click";
        eventLogger_.log(evt);

        return "";
    }

    /**
     * longpress <device> primary|secondary - Simulate a long press.
     */
    std::string handleLongPress(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            return "Usage: longpress <device> primary|secondary";
        }

        int devIdx = std::atoi(tokens[1].c_str());
        if (devIdx < 0 || devIdx >= static_cast<int>(devices_.size())) {
            return "Invalid device index: " + tokens[1];
        }

        std::string button = tokens[2];
        NativeButtonDriver* driver = nullptr;

        if (button == "primary") {
            driver = devices_[devIdx].primaryButtonDriver;
        } else if (button == "secondary") {
            driver = devices_[devIdx].secondaryButtonDriver;
        } else {
            return "Unknown button: " + button + " (use primary or secondary)";
        }

        driver->execCallback(ButtonInteraction::LONG_PRESS);

        // Log a BUTTON_PRESS event
        Event evt;
        evt.timestampMs = devices_[devIdx].clockDriver->milliseconds();
        evt.type = EventType::BUTTON_PRESS;
        evt.deviceIndex = devIdx;
        evt.button = button;
        evt.detail = "longpress";
        eventLogger_.log(evt);

        return "";
    }

    /**
     * assert-state <device> <StateName> - Assert device is in the named state.
     */
    std::string handleAssertState(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            return "Usage: assert-state <device> <StateName>";
        }

        int devIdx = std::atoi(tokens[1].c_str());
        if (devIdx < 0 || devIdx >= static_cast<int>(devices_.size())) {
            return "Invalid device index: " + tokens[1];
        }

        std::string expectedState = tokens[2];
        auto& dev = devices_[devIdx];
        State* currentState = dev.game->getCurrentState();
        int stateId = currentState ? currentState->getStateId() : -1;
        std::string actualState = getStateName(stateId);

        if (actualState != expectedState) {
            return "assert-state failed: device " + tokens[1] +
                   " is in '" + actualState + "', expected '" + expectedState + "'";
        }

        return "";
    }

    /**
     * assert-text <device> "substring" - Assert device display text history
     * contains the given substring.
     */
    std::string handleAssertText(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            return "Usage: assert-text <device> <substring>";
        }

        int devIdx = std::atoi(tokens[1].c_str());
        if (devIdx < 0 || devIdx >= static_cast<int>(devices_.size())) {
            return "Invalid device index: " + tokens[1];
        }

        // Rejoin remaining tokens to support quoted strings with spaces
        std::string needle = rejoinTokens(tokens, 2);
        // Strip surrounding quotes if present
        needle = stripQuotes(needle);

        auto& dev = devices_[devIdx];
        const auto& textHistory = dev.displayDriver->getTextHistory();

        for (const auto& text : textHistory) {
            if (text.find(needle) != std::string::npos) {
                return "";  // Found
            }
        }

        return "assert-text failed: device " + tokens[1] +
               " display does not contain '" + needle + "'";
    }

    /**
     * assert-serial-tx <device> "substring" - Assert device sent serial
     * data containing the given substring (checks EventLogger).
     */
    std::string handleAssertSerialTx(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            return "Usage: assert-serial-tx <device> <substring>";
        }

        int devIdx = std::atoi(tokens[1].c_str());
        if (devIdx < 0 || devIdx >= static_cast<int>(devices_.size())) {
            return "Invalid device index: " + tokens[1];
        }

        std::string needle = rejoinTokens(tokens, 2);
        needle = stripQuotes(needle);

        auto events = eventLogger_.getByTypeAndDevice(EventType::SERIAL_TX, devIdx);
        for (const auto& evt : events) {
            if (evt.message.find(needle) != std::string::npos ||
                evt.detail.find(needle) != std::string::npos) {
                return "";  // Found
            }
        }

        return "assert-serial-tx failed: device " + tokens[1] +
               " has no SERIAL_TX containing '" + needle + "'";
    }

    /**
     * assert-serial-rx <device> "substring" - Assert device received serial
     * data containing the given substring (checks EventLogger).
     */
    std::string handleAssertSerialRx(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            return "Usage: assert-serial-rx <device> <substring>";
        }

        int devIdx = std::atoi(tokens[1].c_str());
        if (devIdx < 0 || devIdx >= static_cast<int>(devices_.size())) {
            return "Invalid device index: " + tokens[1];
        }

        std::string needle = rejoinTokens(tokens, 2);
        needle = stripQuotes(needle);

        auto events = eventLogger_.getByTypeAndDevice(EventType::SERIAL_RX, devIdx);
        for (const auto& evt : events) {
            if (evt.message.find(needle) != std::string::npos ||
                evt.detail.find(needle) != std::string::npos) {
                return "";  // Found
            }
        }

        return "assert-serial-rx failed: device " + tokens[1] +
               " has no SERIAL_RX containing '" + needle + "'";
    }

    /**
     * assert-event <TYPE> [dev=N] [substring] - Assert an event of the
     * given type exists, optionally filtering by device and content.
     */
    std::string handleAssertEvent(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return "Usage: assert-event <TYPE> [dev=N] [substring]";
        }

        EventType type;
        if (!parseEventType(tokens[1], type)) {
            return "Unknown event type: " + tokens[1];
        }

        int devIdx = -1;
        std::string needle;
        parseEventFilters(tokens, 2, devIdx, needle);

        return checkEventExists(type, devIdx, needle, true);
    }

    /**
     * assert-no-event <TYPE> [dev=N] [substring] - Assert no event of the
     * given type exists matching the filters.
     */
    std::string handleAssertNoEvent(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return "Usage: assert-no-event <TYPE> [dev=N] [substring]";
        }

        EventType type;
        if (!parseEventType(tokens[1], type)) {
            return "Unknown event type: " + tokens[1];
        }

        int devIdx = -1;
        std::string needle;
        parseEventFilters(tokens, 2, devIdx, needle);

        return checkEventExists(type, devIdx, needle, false);
    }

    /**
     * advance <ms> - Advance all device clocks by ms, run loops.
     * Same as wait, provided as a debug command alias.
     */
    std::string handleAdvance(const std::vector<std::string>& tokens) {
        return handleWait(tokens);
    }

    /**
     * inspect <device> - Show device state, display text, LED state, etc.
     */
    std::string handleInspect(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return "Usage: inspect <device>";
        }

        int devIdx = std::atoi(tokens[1].c_str());
        if (devIdx < 0 || devIdx >= static_cast<int>(devices_.size())) {
            return "Invalid device index: " + tokens[1];
        }

        // inspect is informational, doesn't fail
        // In headless mode this output goes nowhere, but it's still useful
        // for debugging scripts interactively
        return "";
    }

    /**
     * events [type] [device] - Show recent events (informational).
     */
    std::string handleEvents(const std::vector<std::string>& tokens) {
        // Informational only, never fails
        (void)tokens;
        return "";
    }

    // ==================== LOOP HELPERS ====================

    /**
     * Run one iteration of the main loop:
     * deliver packets, transfer serial, run device loops.
     */
    void runOneLoopIteration() {
        NativePeerBroker::getInstance().deliverPackets();
        SerialCableBroker::getInstance().transferData();

        for (auto& dev : devices_) {
            dev.pdn->loop();
            dev.game->loop();
        }
    }

    // ==================== PARSING HELPERS ====================

    /**
     * Tokenize a command string by spaces, respecting quoted substrings.
     */
    static std::vector<std::string> tokenize(const std::string& cmd) {
        std::vector<std::string> tokens;
        std::string token;
        bool inQuotes = false;

        for (char c : cmd) {
            if (c == '"') {
                inQuotes = !inQuotes;
                token += c;
            } else if (c == ' ' && !inQuotes) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += c;
            }
        }
        if (!token.empty()) {
            tokens.push_back(token);
        }
        return tokens;
    }

    /**
     * Trim leading and trailing whitespace from a string.
     */
    static std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    /**
     * Rejoin tokens from startIndex onward into a single string.
     */
    static std::string rejoinTokens(const std::vector<std::string>& tokens, size_t startIndex) {
        std::string result;
        for (size_t i = startIndex; i < tokens.size(); i++) {
            if (i > startIndex) result += " ";
            result += tokens[i];
        }
        return result;
    }

    /**
     * Strip surrounding double quotes from a string.
     */
    static std::string stripQuotes(const std::string& str) {
        if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
            return str.substr(1, str.size() - 2);
        }
        return str;
    }

    /**
     * Parse an event type name string to an EventType.
     */
    static bool parseEventType(const std::string& name, EventType& out) {
        if (name == "STATE_CHANGE")  { out = EventType::STATE_CHANGE;  return true; }
        if (name == "BUTTON_PRESS")  { out = EventType::BUTTON_PRESS;  return true; }
        if (name == "SERIAL_TX")     { out = EventType::SERIAL_TX;     return true; }
        if (name == "SERIAL_RX")     { out = EventType::SERIAL_RX;     return true; }
        if (name == "ESPNOW_TX")     { out = EventType::ESPNOW_TX;     return true; }
        if (name == "ESPNOW_RX")     { out = EventType::ESPNOW_RX;     return true; }
        if (name == "DISPLAY_TEXT")  { out = EventType::DISPLAY_TEXT;   return true; }
        if (name == "DISPLAY_CLEAR") { out = EventType::DISPLAY_CLEAR;  return true; }
        if (name == "HTTP_REQUEST")  { out = EventType::HTTP_REQUEST;   return true; }
        if (name == "HTTP_RESPONSE") { out = EventType::HTTP_RESPONSE;  return true; }
        return false;
    }

    /**
     * Parse optional dev=N and substring filters from tokens.
     */
    static void parseEventFilters(const std::vector<std::string>& tokens,
                                  size_t startIndex,
                                  int& devIdx,
                                  std::string& needle) {
        devIdx = -1;
        needle = "";

        for (size_t i = startIndex; i < tokens.size(); i++) {
            if (tokens[i].substr(0, 4) == "dev=") {
                devIdx = std::atoi(tokens[i].c_str() + 4);
            } else {
                // Accumulate remaining as needle
                if (!needle.empty()) needle += " ";
                needle += tokens[i];
            }
        }

        needle = stripQuotes(needle);
    }

    /**
     * Check whether a matching event exists (or does not exist).
     * If expectExists is true, returns error when not found.
     * If expectExists is false, returns error when found.
     */
    std::string checkEventExists(EventType type, int devIdx,
                                 const std::string& needle,
                                 bool expectExists) {
        std::vector<Event> events;
        if (devIdx >= 0) {
            events = eventLogger_.getByTypeAndDevice(type, devIdx);
        } else {
            events = eventLogger_.getByType(type);
        }

        bool found = false;
        if (needle.empty()) {
            found = !events.empty();
        } else {
            for (const auto& evt : events) {
                if (evt.message.find(needle) != std::string::npos ||
                    evt.detail.find(needle) != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }

        if (expectExists && !found) {
            std::string desc = eventTypeName(type);
            if (devIdx >= 0) desc += " dev=" + std::to_string(devIdx);
            if (!needle.empty()) desc += " '" + needle + "'";
            return "assert-event failed: no matching event (" + desc + ")";
        }

        if (!expectExists && found) {
            std::string desc = eventTypeName(type);
            if (devIdx >= 0) desc += " dev=" + std::to_string(devIdx);
            if (!needle.empty()) desc += " '" + needle + "'";
            return "assert-no-event failed: event exists (" + desc + ")";
        }

        return "";
    }
};

} // namespace cli

#endif // NATIVE_BUILD
