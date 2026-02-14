#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "game/match-manager.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "utils/simple-timer.hpp"

enum HandshakeStateId {
    HANDSHAKE_INITIATE_STATE = 9,
    BOUNTY_SEND_CC_STATE = 10,
    HUNTER_SEND_ID_STATE = 11,
    CONNECTION_SUCCESSFUL = 12
};
    
    class HandshakeInitiateState : public State {
    public:
        explicit HandshakeInitiateState(Player *player);
        ~HandshakeInitiateState();
    
        void onStateMounted(Device *PDN) override;
        void onStateLoop(Device *PDN) override;
        void onStateDismounted(Device *PDN) override;
        bool transitionToBountySendCC();
        bool transitionToHunterSendId();
    
    private:
        Player* player;
        SimpleTimer handshakeSettlingTimer;
        const int HANDSHAKE_SETTLE_TIME = 500;
        bool transitionToBountySendCCState = false;
        bool transitionToHunterSendIdState = false;
    };
    
    class BountySendConnectionConfirmedState : public State {
    public:
        BountySendConnectionConfirmedState(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager);
        ~BountySendConnectionConfirmedState();
        void onQuickdrawCommandReceived(QuickdrawCommand command);
        void onStateMounted(Device *PDN) override;
        void onStateLoop(Device *PDN) override;
        void onStateDismounted(Device *PDN) override;
        bool transitionToConnectionSuccessful();
    
    private:
        Player* player;
        MatchManager* matchManager;
        QuickdrawWirelessManager* quickdrawWirelessManager;
        SimpleTimer delayTimer;
        const int delay = 100;
        bool transitionToConnectionSuccessfulState = false;
    };
    
    class HunterSendIdState : public State {
    public:
        HunterSendIdState(Player *player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager);
        ~HunterSendIdState();
    
        void onStateMounted(Device *PDN) override;
        void onStateLoop(Device *PDN) override;
        void onStateDismounted(Device *PDN) override;
        void onQuickdrawCommandReceived(QuickdrawCommand command);
        bool transitionToConnectionSuccessful();
    
    private:
        Player* player;
        MatchManager* matchManager;
        QuickdrawWirelessManager* quickdrawWirelessManager;
        SimpleTimer delayTimer;
        const int delay = 100;
        bool transitionToConnectionSuccessfulState = false;
    };

class ConnectionSuccessful : public State {
    public:
        explicit ConnectionSuccessful(Player *player);
        ~ConnectionSuccessful();
    
        void onStateMounted(Device *PDN) override;
        void onStateLoop(Device *PDN) override;
        void onStateDismounted(Device *PDN) override;
        bool transitionToCountdown();
    
    private:
        Player *player;
        bool lightsOn = true;
        int flashDelay = 400;
        uint8_t transitionThreshold = 12;
        uint8_t alertCount = 0;
        SimpleTimer flashTimer;
    };