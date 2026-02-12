#pragma once

#include <memory>
#include <string>
#include <iostream>
#include <cstdint>
#include <set>

enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};

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

    // Challenge/FDN system
    std::string pendingCdevMessage;
    bool pendingChallenge = false;
    uint8_t konamiProgress = 0;  // Bitmask of unlocked Konami buttons
    bool konamiBoon = false;  // True when Konami puzzle "complete" (auto-set when all 7 collected)
    int equippedColorProfile = -1;  // -1 = none, otherwise GameType value
    std::set<int> colorProfileEligibility;  // GameTypes with hard mode beaten
    int lastFdnGameType = -1;  // GameType of last FDN encounter (set by FdnDetected)
    int pendingProfileGame = -1;  // Set by FdnComplete for ColorProfilePrompt
    uint8_t lastFdnReward = 0;  // KonamiButton value of last FDN reward (set by FdnDetected)
    bool recreationalMode = false;  // True when replaying already-beaten content

public:
    // Pending FDN challenge (set by Idle, read by FdnDetected)
    void setPendingChallenge(const std::string& cdevMessage) {
        pendingCdevMessage = cdevMessage;
        pendingChallenge = true;
    }
    bool hasPendingChallenge() const { return pendingChallenge; }
    const std::string& getPendingCdevMessage() const { return pendingCdevMessage; }
    void clearPendingChallenge() {
        pendingCdevMessage.clear();
        pendingChallenge = false;
    }

    // Konami progress
    void unlockKonamiButton(uint8_t buttonIndex) {
        konamiProgress |= (1 << buttonIndex);
    }
    bool hasUnlockedButton(uint8_t buttonIndex) const {
        return (konamiProgress & (1 << buttonIndex)) != 0;
    }
    uint8_t getKonamiProgress() const { return konamiProgress; }
    void setKonamiProgress(uint8_t progress) { konamiProgress = progress; }
    bool isKonamiComplete() const { return (konamiProgress & 0x7F) == 0x7F; }

    // Konami boon (auto-set when all 7 buttons collected)
    bool hasKonamiBoon() const { return konamiBoon; }
    void setKonamiBoon(bool boon) { konamiBoon = boon; }

    // Last FDN game type (set by FdnDetected, read by FdnComplete)
    int getLastFdnGameType() const { return lastFdnGameType; }
    void setLastFdnGameType(int gameType) { lastFdnGameType = gameType; }

    // Pending profile game (set by FdnComplete for ColorProfilePrompt)
    int getPendingProfileGame() const { return pendingProfileGame; }
    void setPendingProfileGame(int gameType) { pendingProfileGame = gameType; }

    // Last FDN reward (set by FdnDetected, read by FdnReencounter)
    uint8_t getLastFdnReward() const { return lastFdnReward; }
    void setLastFdnReward(uint8_t reward) { lastFdnReward = reward; }

    // Recreational mode (set by FdnReencounter, read by FdnComplete)
    bool isRecreationalMode() const { return recreationalMode; }
    void setRecreationalMode(bool mode) { recreationalMode = mode; }

    // Color profile eligibility
    void addColorProfileEligibility(int gameTypeValue) {
        colorProfileEligibility.insert(gameTypeValue);
    }
    bool hasColorProfileEligibility(int gameTypeValue) const {
        return colorProfileEligibility.count(gameTypeValue) > 0;
    }
    const std::set<int>& getColorProfileEligibility() const { return colorProfileEligibility; }
    int getEquippedColorProfile() const { return equippedColorProfile; }
    void setEquippedColorProfile(int gameTypeValue) { equippedColorProfile = gameTypeValue; }
};
