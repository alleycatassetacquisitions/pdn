#pragma once

#include "state/state.hpp"
#include "device/pdn.hpp"
#include "state/connect-state.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp" 
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "device/drivers/peer-comms-types.hpp"

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
        /// FDN input jack that assigned the current target symbol.
        SerialIdentifier fdnTargetPort = SerialIdentifier::INPUT_JACK;
        SymbolId fdnSymbol;
        bool fdnSymbolValid = false;
    
        SimpleTimer bufferTimer;
        const int BUFFER_TIMEOUT = 500;
        SimpleTimer hapticPulseTimer;
        const int HAPTIC_PULSE_DURATION = 100;
    
        bool symbolSent = false;
        bool hapticPulseActive = false;
        bool matchReady = false;
        bool transitionToIdleState = false;
        bool transitionToSymbolMatchedState = false;
        bool symbolLightsActive = false;

        AnimationConfig cfg{};
    
        void renderSymbolSteady(PDN* pdn);
        void renderSymbolDisplay(PDN* pdn);
        void applySteadySymbolLights(PDN* pdn);
        void renderSendConfirmation(PDN* pdn);
        void sendSymbolToFDN();
        void onSymbolMatchCommandReceived(SymbolMatchCommand command);
    };
    
class SymbolMatchedState : public ConnectState<PDN> {
    public:
        SymbolMatchedState(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager, ControllerWirelessManager* controllerWirelessManager);
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
        ControllerWirelessManager* controllerWirelessManager;
    
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
        void onGameSelectCommandReceived(GameSelectCommand command);
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
    explicit Controller1State(ControllerWirelessManager* controllerWirelessManager);
    ~Controller1State();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    void sendButtonMessage(ButtonIdentifier buttonId, ButtonInteraction interaction);

private:
    // Context struct passed as void* to parameterized button callbacks.
    // Stores enough information to call sendButtonMessage with the correct interaction.
    struct ButtonCallbackCtx {
        Controller1State* state;
        ButtonIdentifier  buttonId;
        ButtonInteraction interaction;
    };

    static constexpr int kNumInteractions = static_cast<int>(ButtonInteraction::BUTTON_INTERACTION_COUNT);

    ControllerWirelessManager* controllerWirelessManager;
    ButtonIdentifier lastPressedButton = ButtonIdentifier::PRIMARY_BUTTON;

    struct PendingPeripheral {
        bool         hasPending = false;
        PeripheralCmd command;
        uint8_t       param1;
        uint8_t       param2;
    };

    // Pre-allocated callback contexts: [0..6] primary button, [7..13] secondary button
    ButtonCallbackCtx buttonCtxs_[2 * kNumInteractions];

    PendingPeripheral pendingPeripheral_;
    SimpleTimer       hapticBlinkTimer_;
    SimpleTimer       ledBlinkTimer_;

    void onGameResponseCommandReceived(GameResponseCommand command);
    void onPeripheralCommandReceived(PeripheralCmd command, uint8_t param1, uint8_t param2);
    void executePeripheralCommand(PDN* pdn, PeripheralCmd command, uint8_t param1, uint8_t param2);
};