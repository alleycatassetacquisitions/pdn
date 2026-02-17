#ifdef NATIVE_BUILD

#include "cli/commands/help-commands.hpp"

namespace cli {

CommandResult HelpCommands::cmdHelp(const std::vector<std::string>& /*tokens*/) {
    CommandResult result;
    result.message = "Keys: LEFT/RIGHT=select, UP/DOWN=buttons | Cmds: help, quit, list, select, add, b/l, b2/l2, cable, peer, display, mirror, captions, reboot, role, games, stats, progress, colors, difficulty, demo, duel, rematch";
    return result;
}

CommandResult HelpCommands::cmdHelp2(const std::vector<std::string>& /*tokens*/) {
    CommandResult result;
    result.message = "add [hunter|bounty|npc <game>|challenge <game>] - add device | cable <a> <b> - connect | peer <src> <dst> <type> - send packet | reboot [dev] - restart device | games - list games | stats [dev] [summary|detailed|streaks|reset] - FDN game statistics | progress [dev] - Konami grid | colors [dev] - color profiles | difficulty [dev|reset] - show/reset auto-scaling | demo [game] [easy|hard] - play demo mode | duel [history|record|series] - duel tracking | rematch - rematch last opponent";
    return result;
}

CommandResult HelpCommands::cmdQuit(const std::vector<std::string>& /*tokens*/) {
    CommandResult result;
    result.shouldQuit = true;
    result.message = "Exiting...";
    return result;
}

} // namespace cli

#endif // NATIVE_BUILD
