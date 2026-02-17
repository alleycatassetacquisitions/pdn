#pragma once

#ifdef NATIVE_BUILD

#include <vector>
#include <string>
#include "command-result.hpp"

namespace cli {

class HelpCommands {
public:
    static CommandResult cmdHelp(const std::vector<std::string>& tokens);
    static CommandResult cmdHelp2(const std::vector<std::string>& tokens);
    static CommandResult cmdQuit(const std::vector<std::string>& tokens);
};

} // namespace cli

#endif // NATIVE_BUILD
