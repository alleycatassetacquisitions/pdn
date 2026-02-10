#pragma once

#include <memory>
#include <string>
#include <iostream>
#include <vector>
#include "device/device-types.hpp"

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

// All 7 Konami buttons unlocked = bits 0-6 set = 0x7F
static constexpr uint8_t KONAMI_ALL_UNLOCKED = 0x7F;

class Player {
public:
    Player() = default;
    ~Player() = default;
    
    Player(const std::string& id, Allegiance allegiance, bool isHunter);
    
    std::string toJson() const;

    void fromJson(const std::string &json);

    bool isHunter() const;

    void setIsHunter(bool isHunter);

    void toggleHunter();

    Allegiance getAllegiance() const;

    void setAllegiance(const std::string& allegianceStr);

    void setAllegiance(int allegiance);

    void setAllegiance(Allegiance allegiance);

    std::string getAllegianceString() const;

    std::string getName() const;

    void setName(const std::string& name);

    std::string getFaction() const;

    void setFaction(const std::string& faction);

    void setUserID(char *newId);

    std::string getUserID() const;

    void clearUserID();

    void setCurrentMatchId(const std::string& matchId);

    std::string& getCurrentMatchId();
    const std::string& getCurrentMatchId() const;

    void setCurrentOpponentId(const std::string& opponentId);
    std::string& getCurrentOpponentId();
    const std::string& getCurrentOpponentId() const;

    void setOpponentMacAddress(const std::string& macAddress);

    std::string& getOpponentMacAddress();
    const std::string& getOpponentMacAddress() const;

    unsigned long getLastReactionTime();

    unsigned long getAverageReactionTime();

    int getStreak();

    int getMatchesPlayed();

    int getWins();

    int getLosses();

    void incrementStreak();

    void resetStreak();

    void incrementMatchesPlayed();

    void incrementWins();

    void incrementLosses();

    void addReactionTime(unsigned long reactionTime);

    // Konami progress API
    void unlockKonamiButton(KonamiButton button);
    bool hasUnlockedButton(KonamiButton button) const;
    uint8_t getKonamiProgress() const;
    void setKonamiProgress(uint8_t progress);
    bool hasAllKonamiButtons() const;
    int getUnlockedButtonCount() const;

    // Pending challenge (set by Idle, read by ChallengeDetected)
    void setPendingChallenge(const std::string& cdevMessage);
    const std::string& getPendingCdevMessage() const;
    void clearPendingChallenge();
    bool hasPendingChallenge() const;

    // Color profile API
    void setEquippedColorProfile(GameType game);
    GameType getEquippedColorProfile() const;
    void addColorProfileEligibility(GameType game);
    bool hasColorProfileEligibility(GameType game) const;
    const std::vector<GameType>& getColorProfileEligibility() const;

private:
    std::string id = "default";
    std::string name = "";
    std::string allegianceStr = "none";
    std::string faction = "";

    int winStreak = 0;

    int matchesPlayed = 0;

    int wins = 0;

    int losses = 0;

    unsigned long lastReactionTime = 0;

    unsigned long totalReactionTime = 0;

    Allegiance allegiance = Allegiance::RESISTANCE;

    std::string currentMatchId;
    std::string currentOpponentId;
    std::string opponentMacAddress;
    
    bool hunter = true;

    // Konami progress bitmask — each bit = one button unlocked (bits 0-6)
    uint8_t konamiProgress = 0;

    // Color profile — GameType::QUICKDRAW means no custom profile equipped
    GameType equippedColorProfile = GameType::QUICKDRAW;

    // Color profile eligibility — list of GameType values for which hard mode was beaten
    std::vector<GameType> colorProfileEligibility;

    // Pending challenge state
    std::string pendingCdevMessage;
};
