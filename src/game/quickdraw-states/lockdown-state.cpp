// #include "game/quickdraw-resources.hpp"
// #include "game/quickdraw-states.hpp"
// //
// // Created by Elli Furedy on 1/24/2025.
// //
// Lockdown::Lockdown(Player* player) : State(LOCKED_DOWN) {
//     this->player = player;
//
//     std::vector<const string *> writing;
//     std::vector<const string *> reading;
//
//     if (player->isHunter()) {
//         reading.push_back(&BOUNTY_BATTLE_MESSAGE);
//         writing.push_back(&HUNTER_BATTLE_MESSAGE);
//     } else {
//         reading.push_back(&HUNTER_BATTLE_MESSAGE);
//         writing.push_back(&BOUNTY_BATTLE_MESSAGE);
//     }
//
//     registerValidMessages(reading);
// }
//
// Idle::~Idle() {
//     player = nullptr;
// }
//
// void Idle::onStateMounted(Device *PDN) {
//
//     if (player->isHunter()) {
//         currentPalette = hunterColors;
//     } else {
//         currentPalette = bountyColors;
//     }
//
//     PDN->
//     invalidateScreen()->
//     drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
//     render();
//
//     PDN->setButtonClick(
//         ButtonInteraction::DURING_LONG_PRESS,
//         ButtonIdentifier::PRIMARY_BUTTON,
//         [] {
//             RemotePlayerManager::GetInstance()->Update(true);
//     });
//
//     PDN->setButtonClick(
//         ButtonInteraction::RELEASE,
//         ButtonIdentifier::PRIMARY_BUTTON,
//         [] {
//             RemotePlayerManager::GetInstance()->Update(false);
//     });
// }
//
// void Idle::onStateLoop(Device *PDN) {
//
//     // EVERY_N_MILLIS(250) {
//     //     PDN->writeString(&responseStringMessages[0]);
//     // }
//
//
//     EVERY_N_MILLIS(16) {
//         ledAnimation(PDN);
//     }
//
//     // string *validMessage = waitForValidMessage(PDN);
//     // if (validMessage != nullptr) {
//     //     transitionToHandshakeState = true;
//     // }
//     EVERY_N_SECONDS(3) {
//         if(playerManager->hasRemotePings()) {
//             ESP_LOGI("PDN", "Seeing remote pings.\n");
//             if(playerManager->getTopPingedByPlayer().numPings > 6) {
//                 transitionToLockdownState = true;
//             }
//         }
//     }
//
// }
//
// void Idle::onStateDismounted(Device *PDN) {
//     transitionToHandshakeState = false;
//     PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
//     // PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
// }