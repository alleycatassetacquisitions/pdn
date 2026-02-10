#pragma once

#include "game/player.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/wireless-manager.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/drivers/logger.hpp"

/*
 * ProgressManager persists Konami progress to NVS storage and uploads
 * it to the server when connectivity is available. Follows the same
 * pattern as MatchManager: initialized with Player* + StorageInterface*,
 * uses NVS for local persistence, and defers server uploads until WiFi
 * is available.
 *
 * NVS keys (shared storage driver, distinct key namespace):
 *   "konami"  — uint8_t bitmask of unlocked Konami buttons
 *   "synced"  — uint8_t (0 or 1), whether current progress has been uploaded
 *
 * These keys do not collide with MatchManager's keys ("count", "match_0", etc.).
 */
class ProgressManager {
public:
    ProgressManager();
    ~ProgressManager();

    /*
     * Initialize with references to the player and storage driver.
     * Must be called before any other method.
     */
    void initialize(Player* player, StorageInterface* storage);

    /*
     * Save current Konami progress from Player to NVS.
     * Called after a Konami button is unlocked (e.g., player wins a minigame).
     * Marks progress as unsynced so it will be uploaded on next opportunity.
     */
    void saveProgress();

    /*
     * Load Konami progress from NVS into Player.
     * Called as fallback when server is unreachable during login.
     * Only updates Player if NVS has data (non-zero progress).
     */
    void loadProgress();

    /*
     * Check if there's progress that hasn't been uploaded to the server.
     */
    bool hasUnsyncedProgress() const;

    /*
     * Upload current Konami progress to the server via PUT /api/players/{id}/progress.
     * On success, marks progress as synced in NVS.
     * Returns true if upload succeeded, false if failed or no connectivity.
     */
    bool uploadProgress(WirelessManager* wirelessManager);

    /*
     * Clear all progress data from NVS.
     */
    void clearProgress();

private:
    Player* player = nullptr;
    StorageInterface* storage = nullptr;
    bool synced = true;

    static constexpr const char* PROGRESS_TAG = "ProgressManager";
    static constexpr const char* NVS_KEY_KONAMI = "konami";
    static constexpr const char* NVS_KEY_SYNCED = "synced";
};
