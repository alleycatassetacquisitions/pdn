#pragma once

#include <array>
#include <optional>
#include <string>
#include "device/drivers/button.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/drivers/storage-interface.hpp"
#include "game/stored-match-upload-source.hpp"

inline constexpr const char NODE_MATCHES_PREFS_NAMESPACE[] = "node_matches";
inline constexpr unsigned long NODE_SYNC_WINDOW_MS = 250;

enum class DuelLinkSide {
    Initiator,
    Responder,
};

struct NodeLastMatchDisplay {
    unsigned long myTimeMs = 0;
    unsigned long opponentTimeMs = 0;
    bool hasData = false;
};

struct NodeActiveDuelState {
    bool matchIsReady = false;
    bool hasReceivedDrawResult = false;
    bool hasPressedButton = false;
    bool gracePeriodExpiredNoResult = false;
    bool opponentNeverPressed = false;
    unsigned long duelLocalStartTime = 0;
    unsigned long BUTTON_MASHER_PENALTY_MS = 75;
    int buttonMasherCount = 0;
    DuelLinkSide duelLinkSide = DuelLinkSide::Responder;
    std::optional<Match> match;
    std::array<uint8_t, 6> opponentMac = {};

    unsigned long calculateButtonMasherPenalty() {
        return BUTTON_MASHER_PENALTY_MS * buttonMasherCount;
    }
};

/**
 * Event / NodeNetwork match lifecycle. Initiator maps to Match hunter slot;
 * wire QuickdrawPacket.isHunter means isInitiator.
 */
class NodeMatchManager : public StoredMatchUploadSource {
public:
    NodeMatchManager();
    ~NodeMatchManager();

    void initialize(Player* player, StorageInterface* storage, QuickdrawWirelessManager* quickdrawWirelessManager);

    void initializeMatch(uint8_t* opponentMac);

    void receiveMatch(const char* matchId, const char* opponentId, bool opponentIsInitiator, uint8_t* opponentMac);

    bool isMatchReady();

    bool finalizeMatch();

    bool setHunterDrawTime(unsigned long hunterTimeMs);
    bool setBountyDrawTime(unsigned long bountyTimeMs);

    void setDuelLocalStartTime(unsigned long localStartTimeMs);
    unsigned long getDuelLocalStartTime();

    bool didSync();

    bool linkFailed();

    unsigned long syncDeltaMs() const;

    unsigned getFailedAttempts() const { return failedAttempts_; }

    void recordLinkOutcome();

    bool matchResultsAreIn();

    bool getHasReceivedDrawResult();
    bool getHasPressedButton();

    std::optional<Match>& getCurrentMatch() { return activeDuelState.match; }

    std::string toJson() override;
    void clearStorage() override;
    size_t getStoredMatchCount() override;

    void clearCurrentMatch();

    const NodeLastMatchDisplay& getLastMatchDisplay() const { return lastMatchDisplay_; }

    void setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc);

    void listenForMatchEvents(const QuickdrawCommand& command);

    parameterizedCallbackFunction getDuelButtonPush();
    parameterizedCallbackFunction getButtonMasher();

    void sendNeverPressed(unsigned long pityTime);

    DuelLinkSide getDuelLinkSide() const { return activeDuelState.duelLinkSide; }

    bool localSideIsInitiator() const {
        return activeDuelState.duelLinkSide == DuelLinkSide::Initiator;
    }

    void setReceivedDrawResult();
    void setReceivedButtonPush();
    void setNeverPressed();
    void setOpponentNeverPressed() { activeDuelState.opponentNeverPressed = true; }

private:
    bool appendMatchToStorage(const Match* match);
    void updateStoredMatchCount(uint8_t count);
    Match* readMatchFromStorage(uint8_t index);

    bool isFromActiveMatchOpponent(const QuickdrawCommand& command) const;

    void sendMatchAck();
    void sendMatchId();
    void primeMatch(const char* matchId, const uint8_t* opponentMac);

    void noteOpponentMac(const uint8_t* opponentMac);

    /** Wire field isHunter — true when sender is initiator (hunter slot). */
    bool wireFlagForLocalSide() const { return localSideIsInitiator(); }

    Player* player = nullptr;
    unsigned failedAttempts_ = 0;
    std::array<uint8_t, 6> lastOpponentMac_ = {};
    bool hasLastOpponentMac_ = false;
    RemoteDeviceCoordinator* rdc_ = nullptr;
    NodeActiveDuelState activeDuelState;
    NodeLastMatchDisplay lastMatchDisplay_;
    parameterizedCallbackFunction duelButtonPush;
    parameterizedCallbackFunction buttonMasher;
    StorageInterface* storage = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
};
