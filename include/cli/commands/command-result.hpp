#pragma once

#ifdef NATIVE_BUILD

#include <string>

namespace cli {

/**
 * Result of command execution.
 */
struct CommandResult {
    std::string message;      // Message to display to user
    bool shouldQuit = false;  // Whether the application should exit
};

} // namespace cli

#endif // NATIVE_BUILD
