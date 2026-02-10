#include "game/progress-manager.hpp"

ProgressManager::ProgressManager()
    : player(nullptr)
    , storage(nullptr)
    , synced(true) {
}

ProgressManager::~ProgressManager() {
    player = nullptr;
    storage = nullptr;
}

void ProgressManager::initialize(Player* player, StorageInterface* storage) {
    this->player = player;
    this->storage = storage;

    // Load sync state from NVS
    synced = (storage->readUChar(NVS_KEY_SYNCED, 1) == 1);

    LOG_I(PROGRESS_TAG, "Initialized. Synced: %s", synced ? "true" : "false");
}

void ProgressManager::saveProgress() {
    if (!player || !storage) {
        LOG_E(PROGRESS_TAG, "Cannot save progress — not initialized");
        return;
    }

    uint8_t progress = player->getKonamiProgress();
    storage->writeUChar(NVS_KEY_KONAMI, progress);
    storage->writeUChar(NVS_KEY_SYNCED, 0);
    synced = false;

    LOG_I(PROGRESS_TAG, "Saved progress to NVS: 0x%02X (unsynced)", progress);
}

void ProgressManager::loadProgress() {
    if (!player || !storage) {
        LOG_E(PROGRESS_TAG, "Cannot load progress — not initialized");
        return;
    }

    uint8_t progress = storage->readUChar(NVS_KEY_KONAMI, 0);
    if (progress > 0) {
        player->setKonamiProgress(progress);
        LOG_I(PROGRESS_TAG, "Loaded progress from NVS: 0x%02X", progress);
    } else {
        LOG_I(PROGRESS_TAG, "No progress in NVS (default 0)");
    }
}

bool ProgressManager::hasUnsyncedProgress() const {
    return !synced;
}

bool ProgressManager::uploadProgress(WirelessManager* wirelessManager) {
    if (!player || !storage || !wirelessManager) {
        LOG_E(PROGRESS_TAG, "Cannot upload progress — not initialized");
        return false;
    }

    if (synced) {
        LOG_I(PROGRESS_TAG, "Progress already synced — skipping upload");
        return true;
    }

    // Build progress JSON
    std::string progressJson = R"({"konami_progress":)" +
        std::to_string(player->getKonamiProgress()) + "}";

    bool uploadSucceeded = false;

    QuickdrawRequests::updateProgress(
        wirelessManager,
        player->getUserID(),
        progressJson,
        [this, &uploadSucceeded](const std::string& response) {
            LOG_I(PROGRESS_TAG, "Progress uploaded successfully");
            storage->writeUChar(NVS_KEY_SYNCED, 1);
            synced = true;
            uploadSucceeded = true;
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(PROGRESS_TAG, "Progress upload failed: %s", error.message.c_str());
        }
    );

    return uploadSucceeded;
}

void ProgressManager::clearProgress() {
    if (!storage) {
        LOG_E(PROGRESS_TAG, "Cannot clear progress — not initialized");
        return;
    }

    storage->remove(NVS_KEY_KONAMI);
    storage->remove(NVS_KEY_SYNCED);
    synced = true;

    LOG_I(PROGRESS_TAG, "Cleared progress from NVS");
}
