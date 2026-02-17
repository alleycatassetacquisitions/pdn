#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "command-result.hpp"
#include "cli/cli-renderer.hpp"

namespace cli {

class DisplayCommands {
public:
    static CommandResult cmdMirror(const std::vector<std::string>& tokens, Renderer& renderer);
    static CommandResult cmdCaptions(const std::vector<std::string>& tokens, Renderer& renderer);
    static CommandResult cmdDisplay(const std::vector<std::string>& tokens, Renderer& renderer);
};

} // namespace cli

#endif // NATIVE_BUILD
