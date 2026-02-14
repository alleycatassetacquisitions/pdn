#pragma once

#include <cstdint>
#include <string>

enum class DeviceType : uint8_t {
    PLAYER = 0,
    FDN = 1,
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
        case GameType::GHOST_RUNNER:      return KonamiButton::START;
        case GameType::SPIKE_VECTOR:      return KonamiButton::DOWN;
        case GameType::FIREWALL_DECRYPT:  return KonamiButton::LEFT;
        case GameType::CIPHER_PATH:       return KonamiButton::RIGHT;
        case GameType::EXPLOIT_SEQUENCER: return KonamiButton::B;
        case GameType::BREACH_DEFENSE:    return KonamiButton::A;
        case GameType::SIGNAL_ECHO:       return KonamiButton::UP;
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
 * Lookup: GameType -> AppConfig StateId for that game.
 * Returns the StateId used in AppConfig registration.
 * Returns -1 if no app is registered for this game type.
 */
inline int getAppIdForGame(GameType type) {
    switch (type) {
        case GameType::SIGNAL_ECHO:       return 2;  // SIGNAL_ECHO_APP_ID
        case GameType::FIREWALL_DECRYPT:  return 3;  // FIREWALL_DECRYPT_APP_ID
        case GameType::GHOST_RUNNER:      return 4;  // GHOST_RUNNER_APP_ID
        case GameType::SPIKE_VECTOR:      return 5;  // SPIKE_VECTOR_APP_ID
        case GameType::CIPHER_PATH:       return 6;  // CIPHER_PATH_APP_ID
        case GameType::EXPLOIT_SEQUENCER: return 7;  // EXPLOIT_SEQUENCER_APP_ID
        case GameType::BREACH_DEFENSE:    return 8;  // BREACH_DEFENSE_APP_ID
        default:                          return -1;
    }
}

/*
 * Check if a pairing code is a reserved FDN device code (7001-7007).
 */
inline bool isFdnDeviceCode(const std::string& code) {
    return code == "7001" || code == "7002" || code == "7003" ||
           code == "7004" || code == "7005" || code == "7006" ||
           code == "7007";
}

/*
 * Lookup: game display name string -> GameType
 * Used by CLI commands and --fdn flag to parse user-typed game names.
 * Returns true if name was recognized, fills out gameType.
 */
inline bool parseGameName(const std::string& name, GameType& gameType) {
    if (name == "signal-echo" || name == "signalecho" || name == "7007") {
        gameType = GameType::SIGNAL_ECHO; return true;
    }
    if (name == "ghost-runner" || name == "7001") {
        gameType = GameType::GHOST_RUNNER; return true;
    }
    if (name == "spike-vector" || name == "7002") {
        gameType = GameType::SPIKE_VECTOR; return true;
    }
    if (name == "firewall-decrypt" || name == "7003") {
        gameType = GameType::FIREWALL_DECRYPT; return true;
    }
    if (name == "cipher-path" || name == "7004") {
        gameType = GameType::CIPHER_PATH; return true;
    }
    if (name == "exploit-sequencer" || name == "7005") {
        gameType = GameType::EXPLOIT_SEQUENCER; return true;
    }
    if (name == "breach-defense" || name == "7006") {
        gameType = GameType::BREACH_DEFENSE; return true;
    }
    return false;
}

/*
 * Parse a "fdn:<gameType>:<konamiButton>" message.
 * Returns true if parsing succeeded, fills out gameType and reward.
 */
inline bool parseFdnMessage(const std::string& message, GameType& gameType, KonamiButton& reward) {
    // Expected format: "fdn:<gameType>:<konamiButton>"
    // The prefix "fdn:" is 4 chars
    if (message.size() < 7) return false;  // Minimum: "fdn:X:Y"
    if (message.rfind("fdn:", 0) != 0) return false;

    // Find the two colons after "fdn:"
    size_t firstColon = 3;  // The colon in "fdn:"
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
