#ifdef NATIVE_BUILD

#include "cli/commands/display-commands.hpp"

namespace cli {

CommandResult DisplayCommands::cmdMirror(const std::vector<std::string>& tokens, Renderer& renderer) {
    CommandResult result;
    if (tokens.size() >= 2) {
        std::string arg = tokens[1];
        for (char& c : arg) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        if (arg == "on") {
            renderer.setDisplayMirror(true);
        } else if (arg == "off") {
            renderer.setDisplayMirror(false);
        } else {
            result.message = "Usage: mirror [on|off] - toggles without argument";
            return result;
        }
    } else {
        renderer.toggleDisplayMirror();
    }
    result.message = std::string("Display mirror ") + (renderer.isDisplayMirrorEnabled() ? "ON" : "OFF");
    return result;
}

CommandResult DisplayCommands::cmdCaptions(const std::vector<std::string>& tokens, Renderer& renderer) {
    CommandResult result;
    if (tokens.size() >= 2) {
        std::string arg = tokens[1];
        for (char& c : arg) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        if (arg == "on") {
            renderer.setCaptions(true);
        } else if (arg == "off") {
            renderer.setCaptions(false);
        } else {
            result.message = "Usage: captions [on|off] - toggles without argument";
            return result;
        }
    } else {
        renderer.toggleCaptions();
    }
    result.message = std::string("Captions ") + (renderer.isCaptionsEnabled() ? "ON" : "OFF");
    return result;
}

CommandResult DisplayCommands::cmdDisplay(const std::vector<std::string>& tokens, Renderer& renderer) {
    CommandResult result;
    if (tokens.size() >= 2) {
        std::string arg = tokens[1];
        for (char& c : arg) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        if (arg == "on") {
            renderer.setDisplayMirror(true);
            renderer.setCaptions(true);
        } else if (arg == "off") {
            renderer.setDisplayMirror(false);
            renderer.setCaptions(false);
        } else {
            result.message = "Usage: display [on|off] - toggles without argument";
            return result;
        }
    } else {
        // Toggle: if mirror and captions disagree, force both on
        if (renderer.isDisplayMirrorEnabled() != renderer.isCaptionsEnabled()) {
            renderer.setDisplayMirror(true);
            renderer.setCaptions(true);
        } else {
            renderer.toggleDisplayMirror();
            renderer.toggleCaptions();
        }
    }
    result.message = std::string("Display: Mirror=") +
                     (renderer.isDisplayMirrorEnabled() ? "ON" : "OFF") +
                     " Captions=" +
                     (renderer.isCaptionsEnabled() ? "ON" : "OFF");
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
