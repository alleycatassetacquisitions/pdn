#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

enum class DeviceType : uint8_t {
    PLAYER = 0,
    CHALLENGE = 1,
};

enum class GameType : uint8_t {
    QUICKDRAW = 0,
    GHOST_RUNNER = 1,
    SPIKE_VECTOR = 2,
    FIREWALL_DECRYPT = 3,
    CIPHER_PATH = 4,
    EXPLOIT_SEQUENCER = 5,
    BREACH_DEFENSE = 6,
    SIGNAL_ECHO = 7,
};

enum class KonamiButton : uint8_t {
    UP = 0,
    DOWN = 1,
    LEFT = 2,
    RIGHT = 3,
    B = 4,
    A = 5,
    START = 6,
};

/*
 * Lookup: GameType -> KonamiButton
 * Each minigame awards a specific Konami button when beaten.
 */
inline KonamiButton getRewardForGame(GameType type) {
    switch (type) {
        case GameType::GHOST_RUNNER:      return KonamiButton::UP;
        case GameType::SPIKE_VECTOR:      return KonamiButton::DOWN;
        case GameType::FIREWALL_DECRYPT:  return KonamiButton::LEFT;
        case GameType::CIPHER_PATH:       return KonamiButton::RIGHT;
        case GameType::EXPLOIT_SEQUENCER: return KonamiButton::B;
        case GameType::BREACH_DEFENSE:    return KonamiButton::A;
        case GameType::SIGNAL_ECHO:       return KonamiButton::START;
        default:                          return KonamiButton::START;
    }
}

/*
 * Lookup: GameType -> display name string
 */
inline const char* getGameDisplayName(GameType type) {
    switch (type) {
        case GameType::QUICKDRAW:         return "QUICKDRAW";
        case GameType::GHOST_RUNNER:      return "GHOST RUNNER";
        case GameType::SPIKE_VECTOR:      return "SPIKE VECTOR";
        case GameType::FIREWALL_DECRYPT:  return "FIREWALL DECRYPT";
        case GameType::CIPHER_PATH:       return "CIPHER PATH";
        case GameType::EXPLOIT_SEQUENCER: return "EXPLOIT SEQUENCER";
        case GameType::BREACH_DEFENSE:    return "BREACH DEFENSE";
        case GameType::SIGNAL_ECHO:       return "SIGNAL ECHO";
        default:                          return "UNKNOWN";
    }
}

/*
 * Lookup: KonamiButton -> display name string
 */
inline const char* getKonamiButtonName(KonamiButton button) {
    switch (button) {
        case KonamiButton::UP:    return "UP";
        case KonamiButton::DOWN:  return "DOWN";
        case KonamiButton::LEFT:  return "LEFT";
        case KonamiButton::RIGHT: return "RIGHT";
        case KonamiButton::B:     return "B";
        case KonamiButton::A:     return "A";
        case KonamiButton::START: return "START";
        default:                  return "?";
    }
}

/*
 * Lookup: pairing code string -> GameType
 * Returns QUICKDRAW if the code is not a challenge device code.
 */
inline GameType getGameTypeFromCode(const std::string& code) {
    if (code == "7001") return GameType::GHOST_RUNNER;
    if (code == "7002") return GameType::SPIKE_VECTOR;
    if (code == "7003") return GameType::FIREWALL_DECRYPT;
    if (code == "7004") return GameType::CIPHER_PATH;
    if (code == "7005") return GameType::EXPLOIT_SEQUENCER;
    if (code == "7006") return GameType::BREACH_DEFENSE;
    if (code == "7007") return GameType::SIGNAL_ECHO;
    return GameType::QUICKDRAW;
}

/*
 * Check if a pairing code is a reserved challenge device code (7001-7007).
 */
inline bool isChallengeDeviceCode(const std::string& code) {
    return code == "7001" || code == "7002" || code == "7003" ||
           code == "7004" || code == "7005" || code == "7006" ||
           code == "7007";
}

/*
 * Parse a "cdev:<gameType>:<konamiButton>" message.
 * Returns true if parsing succeeded, fills out gameType and reward.
 */
inline bool parseCdevMessage(const std::string& message, GameType& gameType, KonamiButton& reward) {
    // Expected format: "cdev:<gameType>:<konamiButton>"
    // The prefix "cdev:" is 5 chars
    if (message.size() < 8) return false;  // Minimum: "cdev:X:Y"
    if (message.rfind("cdev:", 0) != 0) return false;

    // Find the two colons after "cdev:"
    size_t firstColon = 4;  // The colon in "cdev:"
    size_t secondColon = message.find(':', firstColon + 1);
    if (secondColon == std::string::npos) return false;

    std::string gameStr = message.substr(firstColon + 1, secondColon - firstColon - 1);
    std::string rewardStr = message.substr(secondColon + 1);

    int gameInt, rewardInt;
    try {
        gameInt = std::stoi(gameStr);
        rewardInt = std::stoi(rewardStr);
    } catch (const std::exception&) {
        return false;
    }

    if (gameInt < 0 || gameInt > 7) return false;
    if (rewardInt < 0 || rewardInt > 6) return false;

    gameType = static_cast<GameType>(gameInt);
    reward = static_cast<KonamiButton>(rewardInt);
    return true;
}

/*
 * Parse a "gres:<won>:<score>" message.
 * Returns true if parsing succeeded, fills out won and score.
 */
inline bool parseGresMessage(const std::string& message, bool& won, int& score) {
    if (message.rfind("gres:", 0) != 0) return false;
    size_t firstColon = 4;
    size_t secondColon = message.find(':', firstColon + 1);
    if (secondColon == std::string::npos) return false;

    std::string wonStr = message.substr(firstColon + 1, secondColon - firstColon - 1);
    std::string scoreStr = message.substr(secondColon + 1);

    won = (wonStr == "1");
    score = std::stoi(scoreStr);
    return true;
}
