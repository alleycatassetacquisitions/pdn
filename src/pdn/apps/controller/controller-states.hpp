#pragma once

#include "state/state.hpp"
#include "device/pdn.hpp"
#include "state/connect-state.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp" 
#include "game/player.hpp"
#include "utils/simple-timer.hpp"

enum ControllerStateId {
    CONTROLLER_SELECT,
    CONTROLLER1,
    SYMBOL,
    SYMBOL_MATCHED,
};

class SymbolState : public ConnectState<PDN> {
    public:
        SymbolState(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager);
        ~SymbolState();
    
        void onStateMounted(PDN* pdn) override;
        void onStateLoop(PDN* pdn) override;
        void onStateDismounted(PDN* pdn) override;
    
        bool isPrimaryRequired() override;
        bool isAuxRequired() override;
    
        bool transitionToIdle();
        bool transitionToSymbolMatched();
    
        void cycleSymbol();
    
    private:
        Player* player;
        SymbolWirelessManager* symbolWirelessManager;
        PDN* mountedPdn = nullptr;
        uint8_t* fdnMac = nullptr;
        /// PDN jack cabled to the FDN (OUTPUT = primary side toward FDN, INPUT = aux side toward FDN).
        SerialIdentifier pdnJackToFdn = SerialIdentifier::OUTPUT_JACK;
        SymbolId fdnSymbol;
    
        SimpleTimer bufferTimer;
        const int BUFFER_TIMEOUT = 500;
        SimpleTimer hapticPulseTimer;
        const int HAPTIC_PULSE_DURATION = 100;
    
        bool symbolSent = false;
        bool hapticPulseActive = false;
        bool matchReady = false;
        bool transitionToIdleState = false;
        bool transitionToSymbolMatchedState = false;
    
        AnimationConfig cfg{};
    
        void renderSymbolSteady(PDN* pdn);
        void renderSendConfirmation(PDN* pdn);
        void sendSymbolToFDN();
        void onSymbolMatchCommandReceived(SymbolMatchCommand command);
    };
    
class SymbolMatchedState : public ConnectState<PDN> {
    public:
        SymbolMatchedState(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager);
        ~SymbolMatchedState();
    
        void onStateMounted(PDN* pdn) override;
        void onStateLoop(PDN* pdn) override;
        void onStateDismounted(PDN* pdn) override;
    
        bool isPrimaryRequired() override;
        bool isAuxRequired() override;
    
        bool transitionToController1();
        bool transitionToSymbol();
        bool transitionToIdle();
    
    private:
        Player* player;
        SymbolWirelessManager* symbolWirelessManager;
    
        bool transitionToController1State = false;
        bool transitionToSymbolState = false;
        bool transitionToIdleState = false;
        bool toggleBlink = true;
        bool holdSteadySymbol = false;
    
        SimpleTimer blinkTimer;
        const int BLINK_INTERVAL = 0.15 * 1000;
        SimpleTimer successTimer;
        const int SUCCESS_TIMEOUT = 3 * 1000;
    
        AnimationConfig cfg{};
    
        void renderSymbolScreen(PDN* pdn);
        void onSymbolMatchCommandReceived(SymbolMatchCommand command);
    };

class ControllerSelectState : public TypedState<PDN> {
public:
    explicit ControllerSelectState();
    ~ControllerSelectState();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToController1();
};

class Controller1State : public TypedState<PDN> {
public:
    explicit Controller1State();
    ~Controller1State();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
};