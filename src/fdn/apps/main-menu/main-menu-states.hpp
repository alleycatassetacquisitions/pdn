// #pragma once

// #include "state/fdn-connect-state.hpp"
// #include "device/fdn.hpp"
// #include "device/remote-device-coordinator.hpp"
// #include "utils/simple-timer.hpp"

// enum MainMenuStateId {
//     MAIN_MENU = 0,
// };

// class MainMenuState : public FDNConnectState {
// public:
//     explicit MainMenuState(RemoteDeviceCoordinator* remoteDeviceCoordinator);
//     ~MainMenuState();

//     void onStateMounted(FDN* fdn) override;
//     void onStateLoop(FDN* fdn) override;
//     void onStateDismounted(FDN* fdn) override;

//     bool transitionToSymbolMatch();
//     void renderScreen(FDN* fdn);

// private:
//     static constexpr int NUM_SCREENS         = 3;
//     static constexpr int DISPLAY_TIMER_MS    = 4000;

//     const char* PHRASES[NUM_SCREENS][3] = {
//         {"YOU'VE HACKED INTO", "ONE OF FIVE",        "FIXED DATA NODES"},
//         {"THE 4th WORD OF",    "THE PASSPHRASE IS:", "ORIGINAL"},
//         {"FIND SPHYNX.",       "RECITE THE PHRASE.", "REAP THE REWARDS."},
//     };

//     SimpleTimer displayTimer;
//     int screenIndex = 0;
// };
