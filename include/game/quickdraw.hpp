#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "apps/player-registration/player-registration.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "wireless/team-packet.hpp"
#include <functional>

constexpr size_t MATCH_SIZE = sizeof(Match);

constexpr int QUICKDRAW_APP_ID = 1;

class Quickdraw : public StateMachine {
public:
    using GameEventCallback = std::function<void(GameEventType)>;

    Quickdraw(Player *player, Device *PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager);
    ~Quickdraw();

    void populateStateMap() override;
    void onStateLoop(Device* PDN) override;

    void onTeamPacketReceived(const uint8_t* src, const uint8_t* data, size_t len);

    bool hasTeamInvite() const { return hasTeamInvite_; }
    const uint8_t* getInviteChampionMac() const { return inviteChampionMac_; }
    uint8_t getInvitePosition() const { return invitePosition_; }
    void setGameEventCallback(GameEventCallback cb) { gameEventCallback_ = cb; }
    void clearGameEventCallback() { gameEventCallback_ = nullptr; }
    bool isInviteRejected() const { return inviteRejected_; }
    int getSupporterCount() const { return supporterMacCount_; }
    int getConfirmedSupporters() const { return confirmedSupporters_; }
    void clearChainState();

    bool shouldTransitionToSupporter(SupporterReady* supporterReady);

private:
    void onStateChanged(int newStateId);
    int findSupporterIndex(const uint8_t* mac) const;

    std::vector<Match> matches;
    int numMatches = 0;
    MatchManager* matchManager;
    Player *player;
    WirelessManager* wirelessManager;
    StorageInterface* storageManager;
    PeerCommsInterface* peerComms;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    RemoteDebugManager* remoteDebugManager;

    // Chain state
    static constexpr int MAX_SUPPORTERS = 255;
    int confirmedSupporters_ = 0;
    uint8_t supporterMacs_[MAX_SUPPORTERS][6] = {};
    int supporterMacCount_ = 0;
    int lastStateId_ = -1;
    bool hasTeamInvite_ = false;
    uint8_t inviteChampionMac_[6] = {};
    uint8_t inviteSenderMac_[6] = {};
    char inviteChampionName_[CHAMPION_NAME_MAX] = {};
    uint8_t invitePosition_ = 0;
    bool inviteChampionIsHunter_ = false;
    bool inviteRejected_ = false;
    GameEventCallback gameEventCallback_ = nullptr;
};
