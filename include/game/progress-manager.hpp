#pragma once

#include "game/player.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include <cstdint>
#include <cstdio>

/*
 * ProgressManager persists Konami progress to NVS storage.
 *
 * NVS keys:
 *   "konami"        — uint8_t bitmask of unlocked Konami buttons
 *   "konami_boon"   — uint8_t (0/1), Konami puzzle complete
 *   "color_profile" — uint8_t (equipped + 1, 0 = none)
 *   "color_elig"    — uint8_t bitmask of eligible color profiles (7 games = 7 bits)
 *   "easy_att"      — uint8_t[7] array of easy mode attempt counts
 *   "hard_att"      — uint8_t[7] array of hard mode attempt counts
 *   "synced"        — uint8_t (0 or 1), whether progress has been uploaded
 */
class ProgressManager {
public:
    ProgressManager() = default;
    ~ProgressManager() = default;

    void initialize(Player* player, StorageInterface* storage) {
        this->player = player;
        this->storage = storage;
    }

    void saveProgress() {
        if (!storage || !player) return;
        storage->writeUChar("konami", player->getKonamiProgress());
        storage->writeUChar("konami_boon", player->hasKonamiBoon() ? 1 : 0);

        // Equipped color profile: store as gameType + 1 (0 = none)
        int equipped = player->getEquippedColorProfile();
        storage->writeUChar("color_profile", static_cast<uint8_t>(equipped + 1));

        // Color profile eligibility: pack set into bitmask (8 game types, 8 bits)
        uint8_t eligMask = 0;
        for (int gameType : player->getColorProfileEligibility()) {
            if (gameType >= 0 && gameType < 8) {
                eligMask |= (1 << gameType);
            }
        }
        storage->writeUChar("color_elig", eligMask);

        // Save attempt arrays (7 bytes each)
        const uint8_t* easyAttempts = player->getEasyAttemptsArray();
        const uint8_t* hardAttempts = player->getHardAttemptsArray();
        for (int i = 0; i < 7; i++) {
            char easyKey[16], hardKey[16];
            snprintf(easyKey, sizeof(easyKey), "easy_att_%d", i);
            snprintf(hardKey, sizeof(hardKey), "hard_att_%d", i);
            storage->writeUChar(easyKey, easyAttempts[i]);
            storage->writeUChar(hardKey, hardAttempts[i]);
        }

        storage->writeUChar("synced", 0);
        synced = false;
    }

    void loadProgress() {
        if (!storage || !player) return;
        uint8_t progress = storage->readUChar("konami", 0);
        if (progress > 0) {
            player->setKonamiProgress(progress);
        }

        uint8_t boon = storage->readUChar("konami_boon", 0);
        player->setKonamiBoon(boon != 0);

        uint8_t profileStored = storage->readUChar("color_profile", 0);
        if (profileStored > 0) {
            player->setEquippedColorProfile(static_cast<int>(profileStored) - 1);
        }

        uint8_t eligMask = storage->readUChar("color_elig", 0);
        for (int i = 0; i < 8; i++) {
            if (eligMask & (1 << i)) {
                player->addColorProfileEligibility(i);
            }
        }

        // Load attempt arrays (7 bytes each)
        for (int i = 0; i < 7; i++) {
            char easyKey[16], hardKey[16];
            snprintf(easyKey, sizeof(easyKey), "easy_att_%d", i);
            snprintf(hardKey, sizeof(hardKey), "hard_att_%d", i);
            uint8_t easyCount = storage->readUChar(easyKey, 0);
            uint8_t hardCount = storage->readUChar(hardKey, 0);
            // Directly set the loaded values
            for (uint8_t j = 0; j < easyCount; j++) {
                player->incrementEasyAttempts(static_cast<GameType>(i));
            }
            for (uint8_t j = 0; j < hardCount; j++) {
                player->incrementHardAttempts(static_cast<GameType>(i));
            }
        }

        uint8_t syncedVal = storage->readUChar("synced", 1);
        synced = (syncedVal != 0);
    }

    bool hasUnsyncedProgress() const { return !synced; }

    /*
     * Check if progress has been synced to the server.
     * Returns true if synced, false if pending upload.
     */
    bool isSynced() const { return synced; }

    /*
     * Mark progress as unsynced (needs upload).
     * Called whenever progress changes locally.
     */
    void markUnsynced() {
        synced = false;
        if (storage) {
            storage->writeUChar("synced", 0);
        }
    }

    void clearProgress() {
        if (!storage) return;
        storage->writeUChar("konami", 0);
        storage->writeUChar("konami_boon", 0);
        storage->writeUChar("color_profile", 0);
        storage->writeUChar("color_elig", 0);
        // Clear attempt arrays
        for (int i = 0; i < 7; i++) {
            char easyKey[16], hardKey[16];
            snprintf(easyKey, sizeof(easyKey), "easy_att_%d", i);
            snprintf(hardKey, sizeof(hardKey), "hard_att_%d", i);
            storage->writeUChar(easyKey, 0);
            storage->writeUChar(hardKey, 0);
        }
        storage->writeUChar("synced", 1);
        synced = true;
    }

    /*
     * Sync progress to server via HTTP PUT /api/progress.
     * Marks progress as synced on success. No-op if already synced.
     */
    void syncProgress(HttpClientInterface* httpClient) {
        if (!httpClient || synced) return;
        if (!httpClient->isConnected()) return;

        // Build progress JSON with attempt arrays and color eligibility
        char body[512];
        std::string easyAttemptsJson = "[";
        std::string hardAttemptsJson = "[";
        uint8_t colorEligibilityMask = 0;

        if (player) {
            for (int i = 0; i < 7; i++) {
                if (i > 0) {
                    easyAttemptsJson += ",";
                    hardAttemptsJson += ",";
                }
                easyAttemptsJson += std::to_string(player->getEasyAttempts(static_cast<GameType>(i)));
                hardAttemptsJson += std::to_string(player->getHardAttempts(static_cast<GameType>(i)));
            }

            // Convert color eligibility set to bitmask
            for (int gameType : player->getColorProfileEligibility()) {
                if (gameType >= 0 && gameType < 8) {
                    colorEligibilityMask |= (1 << gameType);
                }
            }
        } else {
            easyAttemptsJson += "0,0,0,0,0,0,0";
            hardAttemptsJson += "0,0,0,0,0,0,0";
        }
        easyAttemptsJson += "]";
        hardAttemptsJson += "]";

        snprintf(body, sizeof(body),
            R"({"konami":%d,"boon":%s,"profile":%d,"colorEligibility":%d,"easyAttempts":%s,"hardAttempts":%s})",
            player ? player->getKonamiProgress() : 0,
            (player && player->hasKonamiBoon()) ? "true" : "false",
            player ? player->getEquippedColorProfile() : -1,
            colorEligibilityMask,
            easyAttemptsJson.c_str(),
            hardAttemptsJson.c_str());

        HttpRequest request(
            "/api/progress",
            "PUT",
            std::string(body),
            [this](const std::string& response) {
                // Only mark synced if response indicates success
                // Response parsing is handled elsewhere, assume success callback = HTTP 200
                synced = true;
                if (storage) {
                    storage->writeUChar("synced", 1);
                }
            },
            [](const WirelessErrorInfo& error) {
                // Sync failed — will retry on next save
            }
        );

        httpClient->queueRequest(request);
    }

    /*
     * Download progress from server and merge with local data using conflict resolution.
     * - Bitmask fields (konami, colorEligibility): union merge (bitwise OR)
     * - Attempt counts: max-wins (keep higher value for each game/difficulty)
     * - Backward compatibility: if fields missing, don't overwrite local data
     */
    void downloadAndMergeProgress(const std::string& jsonResponse) {
        if (!player) return;

        // Parse JSON response
        // Expected format: {"data":{"konami":127,"boon":true,"profile":3,"colorEligibility":127,"easyAttempts":[...],"hardAttempts":[...]}}

        // Simple JSON parsing (manual, since we're avoiding ArduinoJson dependency in header)
        // Extract konami value
        size_t konamiPos = jsonResponse.find("\"konami\":");
        if (konamiPos != std::string::npos) {
            int serverKonami = std::stoi(jsonResponse.substr(konamiPos + 9));
            // Union merge: OR local and server values
            uint8_t mergedKonami = player->getKonamiProgress() | static_cast<uint8_t>(serverKonami);
            player->setKonamiProgress(mergedKonami);
        }

        // Extract boon value
        size_t boonPos = jsonResponse.find("\"boon\":");
        if (boonPos != std::string::npos) {
            bool serverBoon = jsonResponse.substr(boonPos + 7, 4) == "true";
            // OR for boolean: if either is true, result is true
            if (serverBoon) {
                player->setKonamiBoon(true);
            }
        }

        // Extract profile value
        size_t profilePos = jsonResponse.find("\"profile\":");
        if (profilePos != std::string::npos) {
            int serverProfile = std::stoi(jsonResponse.substr(profilePos + 10));
            // For equipped profile, use server value if local is not set
            if (player->getEquippedColorProfile() == -1 && serverProfile != -1) {
                player->setEquippedColorProfile(serverProfile);
            }
        }

        // Extract colorEligibility
        size_t eligPos = jsonResponse.find("\"colorEligibility\":");
        if (eligPos != std::string::npos) {
            int serverElig = std::stoi(jsonResponse.substr(eligPos + 19));
            // Union merge: OR local and server bitmasks
            std::set<int> mergedElig = player->getColorProfileEligibility();
            for (int i = 0; i < 8; i++) {
                if (serverElig & (1 << i)) {
                    mergedElig.insert(i);
                }
            }
            player->setColorProfileEligibility(mergedElig);
        }

        // Extract easyAttempts array
        size_t easyPos = jsonResponse.find("\"easyAttempts\":[");
        if (easyPos != std::string::npos) {
            size_t startPos = easyPos + 16;
            size_t endPos = jsonResponse.find("]", startPos);
            if (endPos != std::string::npos) {
                std::string arrayStr = jsonResponse.substr(startPos, endPos - startPos);
                // Parse comma-separated values
                size_t pos = 0;
                int gameIdx = 0;
                while (pos < arrayStr.length() && gameIdx < 7) {
                    size_t nextComma = arrayStr.find(",", pos);
                    if (nextComma == std::string::npos) nextComma = arrayStr.length();
                    int serverCount = std::stoi(arrayStr.substr(pos, nextComma - pos));
                    // Max-wins: keep higher value
                    uint8_t localCount = player->getEasyAttempts(static_cast<GameType>(gameIdx));
                    if (serverCount > localCount) {
                        player->setEasyAttempts(static_cast<GameType>(gameIdx), static_cast<uint8_t>(serverCount));
                    }
                    pos = nextComma + 1;
                    gameIdx++;
                }
            }
        }

        // Extract hardAttempts array
        size_t hardPos = jsonResponse.find("\"hardAttempts\":[");
        if (hardPos != std::string::npos) {
            size_t startPos = hardPos + 16;
            size_t endPos = jsonResponse.find("]", startPos);
            if (endPos != std::string::npos) {
                std::string arrayStr = jsonResponse.substr(startPos, endPos - startPos);
                // Parse comma-separated values
                size_t pos = 0;
                int gameIdx = 0;
                while (pos < arrayStr.length() && gameIdx < 7) {
                    size_t nextComma = arrayStr.find(",", pos);
                    if (nextComma == std::string::npos) nextComma = arrayStr.length();
                    int serverCount = std::stoi(arrayStr.substr(pos, nextComma - pos));
                    // Max-wins: keep higher value
                    uint8_t localCount = player->getHardAttempts(static_cast<GameType>(gameIdx));
                    if (serverCount > localCount) {
                        player->setHardAttempts(static_cast<GameType>(gameIdx), static_cast<uint8_t>(serverCount));
                    }
                    pos = nextComma + 1;
                    gameIdx++;
                }
            }
        }

        // After merging, save the merged data locally
        saveProgress();
    }

private:
    Player* player = nullptr;
    StorageInterface* storage = nullptr;
    bool synced = true;
};
