#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <vector>
#include <sys/ioctl.h>

#include "cli/cli-terminal.hpp"
#include "cli/cli-fdn-device.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "state/state.hpp"
#include "state/state-machine.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

namespace cli {

/**
 * Renderer for the FDN CLI simulator.
 * Shows one FDN panel plus compact PDN panels.
 *
 * Selected entity is encoded as:
 *   -1  → FDN
 *   >=0 → PDN device at that index
 */
class FDNRenderer {
public:
    static constexpr int STATUS_START_ROW = 14;

    FDNRenderer() = default;

    void setDisplayMirror(bool v)  { displayMirrorEnabled_ = v; }
    void toggleDisplayMirror()     { displayMirrorEnabled_ = !displayMirrorEnabled_; }
    bool isDisplayMirrorEnabled()  const { return displayMirrorEnabled_; }

    void setCaptions(bool v)  { captionsEnabled_ = v; }
    void toggleCaptions()     { captionsEnabled_ = !captionsEnabled_; }
    bool isCaptionsEnabled()  const { return captionsEnabled_; }

    void renderUI(FDNInstance&                fdn,
                  std::vector<DeviceInstance>& pdns,
                  const std::string&           commandResult,
                  const std::string&           commandBuffer,
                  int                          selectedEntity) {
        currentFrame_.clear();

        // Entity selector bar
        {
            std::string bar = "Devices: ";
            auto addEntry = [&](int id, const char* label, bool selected) {
                if (!bar.empty() && bar.back() != ' ') bar += "  ";
                if (selected) {
                    bar += format("\033[1;7;33m[%d] %s\033[0m", id, label);
                } else {
                    bar += format("\033[90m[%d] %s\033[0m", id, label);
                }
            };
            addEntry(-1, fdn.fdnId.c_str(), selectedEntity == -1);
            for (int i = 0; i < static_cast<int>(pdns.size()); ++i) {
                addEntry(i, pdns[i].deviceId.c_str(), selectedEntity == i);
            }
            bar += "   \033[90m(LEFT/RIGHT to switch)\033[0m";
            bufferLine(bar);
            bufferLine("");
        }

        // Main panel: FDN or selected PDN
        if (selectedEntity == -1) {
            renderFDNPanel(fdn, pdns, /*isSelected=*/true);
        } else if (selectedEntity >= 0 && selectedEntity < static_cast<int>(pdns.size())) {
            renderPDNPanel(pdns[selectedEntity], /*isSelected=*/true);
        }

        bufferLine("");

        if (!commandResult.empty()) {
            bufferLine(format("\033[1;32m> %s\033[0m", commandResult.c_str()));
        } else {
            bufferLine("");
        }

        const char* promptId = (selectedEntity == -1)
            ? fdn.fdnId.c_str()
            : (selectedEntity < static_cast<int>(pdns.size())
                ? pdns[selectedEntity].deviceId.c_str()
                : "---");
        bufferLine(format(
            "\033[1;36m[%s]>\033[0m %s\033[7m \033[0m   "
            "\033[90m(↑↓=buttons, ENTER=tertiary, 'help' for cmds)\033[0m",
            promptId,
            commandBuffer.c_str()));

        renderDiff();
    }

private:
    bool displayMirrorEnabled_ = false;
    bool captionsEnabled_       = true;
    std::vector<std::string> previousFrame_;
    std::vector<std::string> currentFrame_;

    void bufferLine(const std::string& line) {
        currentFrame_.push_back(line);
    }

    static int getTerminalHeight() {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
            return static_cast<int>(ws.ws_row);
        return 50;
    }

    void renderDiff() {
        int termHeight = getTerminalHeight();
        while (previousFrame_.size() < currentFrame_.size()) previousFrame_.push_back("");

        for (size_t i = 0; i < currentFrame_.size(); ++i) {
            int row = STATUS_START_ROW + static_cast<int>(i);
            if (row >= termHeight) break;
            if (i >= previousFrame_.size() || currentFrame_[i] != previousFrame_[i]) {
                Terminal::moveCursor(row, 1);
                Terminal::clearLine();
                printf("%s", currentFrame_[i].c_str());
            }
        }

        if (previousFrame_.size() > currentFrame_.size()) {
            for (size_t i = currentFrame_.size(); i < previousFrame_.size(); ++i) {
                int row = STATUS_START_ROW + static_cast<int>(i);
                if (row >= termHeight) break;
                Terminal::moveCursor(row, 1);
                Terminal::clearLine();
            }
        }

        previousFrame_ = currentFrame_;
        currentFrame_.clear();
        Terminal::flush();
    }

    static std::string truncate(const std::string& s, size_t n) {
        return s.length() <= n ? s : s.substr(0, n - 2) + "..";
    }

    static std::string renderLEDStr(const LEDState::SingleLEDState& led) {
        char buf[64];
        if (led.brightness > 0 && (led.color.red || led.color.green || led.color.blue)) {
            int r = (led.color.red   * led.brightness) / 255;
            int g = (led.color.green * led.brightness) / 255;
            int b = (led.color.blue  * led.brightness) / 255;
            snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm \033[0m", r, g, b);
        } else {
            snprintf(buf, sizeof(buf), "\033[90m.\033[0m");
        }
        return buf;
    }

    // ---- FDN panel ----

    void renderFDNPanel(FDNInstance& fdn,
                        const std::vector<DeviceInstance>& pdns,
                        bool isSelected) {
        ::State* cur = fdn.demoModule ? fdn.demoModule->getCurrentState() : nullptr;
        int stateId = cur ? cur->getStateId() : -1;
        fdn.updateStateHistory(stateId);

        std::string histStr;
        for (size_t i = 0; i < fdn.stateHistory.size(); ++i) {
            if (i) histStr += " -> ";
            histStr += getFDNStateName(fdn.stateHistory[i]);
        }
        if (histStr.empty()) histStr = "(none)";

        const char* headerColor   = isSelected ? "\033[1;35m" : "\033[35m";
        const char* selectedMarker = isSelected ? " *SELECTED*" : "";

        bufferLine(format("%s+-- FDN [%s]%s ----------------------------------------+\033[0m",
                   headerColor, fdn.fdnId.c_str(), selectedMarker));

        // Connection info
        std::string connStr;
        const auto& fdnConns = SerialCableBroker::getInstance().getFDNConnections();
        for (const auto& c : fdnConns) {
            if (c.fdnIndex != fdn.fdnIndex) continue;
            const char* jackLabel = c.useSecondaryJack ? "J2" : "J1";
            if (c.pdnIndex < static_cast<int>(pdns.size())) {
                if (!connStr.empty()) connStr += ", ";
                connStr += format("%s->%s", pdns[c.pdnIndex].deviceId.c_str(), jackLabel);
            }
        }
        if (connStr.empty()) connStr = "(none)";

        bufferLine(format("| App: DemoModule  State: [%d] %-12s  Connected PDNs: %s",
                   stateId, getFDNStateName(stateId), connStr.c_str()));

        bufferLine(format("| History: %s", histStr.c_str()));

        // Display
        {
            const auto& textHistory = fdn.displayDriver->getTextHistory();
            std::vector<std::string> displayRows(4, "");
            for (size_t i = 0; i < textHistory.size() && i < 4; ++i)
                displayRows[i] = truncate(textHistory[i], 40);

            if (displayMirrorEnabled_) {
                auto mirrorLines = fdn.displayDriver->renderToBraille();
                bufferLine(format("| Display: Font=%-6s    Mirror=ON",
                           fdn.displayDriver->getFontModeName().c_str()));
                if (captionsEnabled_) {
                    bufferLine("|  +----------------------------------------------------------------+  Captions:");
                } else {
                    bufferLine("|  +----------------------------------------------------------------+");
                }
                for (size_t i = 0; i < mirrorLines.size(); ++i) {
                    std::string line = "|  |" + mirrorLines[i] + "|";
                    if (captionsEnabled_ && i < 4) {
                        line += "  [" + std::to_string(i) + "] " + displayRows[i];
                    }
                    bufferLine(line);
                }
                bufferLine("|  +----------------------------------------------------------------+");
            } else if (captionsEnabled_) {
                bufferLine(format("| Display: Font=%-6s  Captions:",
                           fdn.displayDriver->getFontModeName().c_str()));
                bufferLine(format("|                       [0] %s",
                           displayRows[0].empty() ? "(blank)" : displayRows[0].c_str()));
                bufferLine(format("|                       [1] %s", displayRows[1].c_str()));
                bufferLine(format("|                       [2] %s", displayRows[2].c_str()));
                bufferLine(format("|                       [3] %s", displayRows[3].c_str()));
            } else {
                std::string d0 = displayRows[0].empty() ? "(blank)" : displayRows[0];
                bufferLine(format("| Display: [0] %-30s  [1] %s", d0.c_str(), displayRows[1].c_str()));
                bufferLine(format("|          [2] %-30s", displayRows[2].c_str()));
            }
        }

        // LEDs (compact)
        std::string ledStr = "| LEDs: Recess[";
        for (int i = 0; i < 9; ++i) ledStr += renderLEDStr(fdn.lightDriver->getRightLights()[i]);
        ledStr += "]  Fin[";
        for (int i = 0; i < 9; ++i) ledStr += renderLEDStr(fdn.lightDriver->getLeftLights()[i]);
        ledStr += "]";
        bufferLine(ledStr);

        bufferLine(format("| Haptics: %-3s  Intensity=%d",
                   fdn.hapticsDriver->isOn() ? "ON" : "OFF",
                   fdn.hapticsDriver->getIntensity()));

        // Serial jack status
        auto inJackStatus = [&](NativeSerialDriver* jack, const char* label) {
            return format("| Serial %s: in=%zu out=%zu  TX: %s  RX: %s",
                label,
                jack->getInputQueueSize(),
                jack->getOutputBufferSize(),
                jack->getSentHistory().empty() ? "(none)" : truncate(jack->getSentHistory().back(), 20).c_str(),
                jack->getReceivedHistory().empty() ? "(none)" : truncate(jack->getReceivedHistory().back(), 20).c_str());
        };
        bufferLine(inJackStatus(fdn.serialInDriver, "IN1 (primary)  "));
        bufferLine(inJackStatus(fdn.serialInSecondaryDriver, "IN2 (secondary)"));

        size_t peerCount   = NativePeerBroker::getInstance().getPeerCount();
        size_t pendingPkts = NativePeerBroker::getInstance().getPendingPacketCount();
        bufferLine(format("| ESP-NOW: MAC=%s  Peers=%zu  Pending=%zu",
                   fdn.peerCommsDriver->getMacString().c_str(), peerCount, pendingPkts));

        bufferLine(format("%s+--------------------------------------------------------------+\033[0m", headerColor));
    }

    // ---- PDN panel (compact) ----

    void renderPDNPanel(DeviceInstance& dev, bool isSelected) {
        ::State* cur = dev.game->getCurrentState();
        int stateId = cur ? cur->getStateId() : -1;
        dev.updateStateHistory(stateId);

        std::string histStr;
        for (size_t i = 0; i < dev.stateHistory.size(); ++i) {
            if (i) histStr += " -> ";
            histStr += getStateName(dev.stateHistory[i]);
        }
        if (histStr.empty()) histStr = "(none)";

        const char* headerColor    = isSelected ? "\033[1;33m" : "\033[33m";
        const char* selectedMarker = isSelected ? " *SELECTED*" : "";

        bufferLine(format("%s+-- PDN [%s] %-6s%s ---------------------------------+\033[0m",
                   headerColor,
                   dev.deviceId.c_str(),
                   dev.isHunter ? "HUNTER" : "BOUNTY",
                   selectedMarker));

        bufferLine(format("| State: [%2d] %-22s  Haptics: %-3s  Intensity=%d",
                   stateId,
                   getStateName(stateId),
                   dev.hapticsDriver->isOn() ? "ON" : "OFF",
                   dev.hapticsDriver->getIntensity()));

        bufferLine(format("| History: %s", histStr.c_str()));

        // Display
        {
            const auto& textHistory = dev.displayDriver->getTextHistory();
            std::vector<std::string> displayRows(4, "");
            for (size_t i = 0; i < textHistory.size() && i < 4; ++i)
                displayRows[i] = truncate(textHistory[i], 40);

            if (displayMirrorEnabled_) {
                auto mirrorLines = dev.displayDriver->renderToBraille();
                bufferLine(format("| Display: Font=%-6s    Mirror=ON",
                           dev.displayDriver->getFontModeName().c_str()));
                bufferLine("|  +----------------------------------------------------------------+");
                for (size_t i = 0; i < mirrorLines.size(); ++i) {
                    std::string line = "|  |" + mirrorLines[i] + "|";
                    if (captionsEnabled_ && i < 4) {
                        line += "  [" + std::to_string(i) + "] " + displayRows[i];
                    }
                    bufferLine(line);
                }
                bufferLine("|  +----------------------------------------------------------------+");
            } else if (captionsEnabled_) {
                bufferLine(format("| Display: Font=%-6s  Captions:",
                           dev.displayDriver->getFontModeName().c_str()));
                bufferLine(format("|   [0] %s", displayRows[0].empty() ? "(blank)" : displayRows[0].c_str()));
                bufferLine(format("|   [1] %s", displayRows[1].c_str()));
                bufferLine(format("|   [2] %s", displayRows[2].c_str()));
            } else {
                std::string d0 = displayRows[0].empty() ? "(blank)" : displayRows[0];
                bufferLine(format("| Display: %s", d0.c_str()));
            }
        }

        // LEDs
        std::string ledStr = "| LEDs: L[";
        for (int i = 8; i >= 0; --i) ledStr += renderLEDStr(dev.lightDriver->getLeftLights()[i]);
        ledStr += "] T[";
        ledStr += renderLEDStr(dev.lightDriver->getTransmitLight());
        ledStr += "] R[";
        for (int i = 8; i >= 0; --i) ledStr += renderLEDStr(dev.lightDriver->getRightLights()[i]);
        ledStr += "]";
        bufferLine(ledStr);

        bufferLine(format("| Serial OUT: in=%zu out=%zu  |  ESP-NOW MAC=%s",
                   dev.serialOutDriver->getInputQueueSize(),
                   dev.serialOutDriver->getOutputBufferSize(),
                   dev.peerCommsDriver->getMacString().c_str()));

        bufferLine(format("%s+--------------------------------------------------------------+\033[0m", headerColor));
    }
};

} // namespace cli

#endif // NATIVE_BUILD
