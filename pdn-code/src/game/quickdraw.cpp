#include "../../include/game/quickdraw.hpp"

Quickdraw::Quickdraw() {
    PDN = Device::GetInstance();
}

std::vector<QuickdrawState> Quickdraw::populateStateMap() {
    
}



void Quickdraw::quickDrawGame() {
  StateMachine::loop();
  // if (currentState == QuickdrawState::DORMANT) {
  //   if (!activationInitiated) {
  //     setupActivation();
  //   }

  //   if (shouldActivate()) {
  //     updateQuickdrawState(QdState::ACTIVATED);
  //   }
  // } else if (currentState == QdState::ACTIVATED) {
  //   if (beginActivationSequence) {
  //     if (activationSequence()) {
  //       beginActivationSequence = false; 
  //     }
  //   }

  //   activationIdle();
  //   if (initiateHandshake()) {
  //     updateQDState(QdState::HANDSHAKE);
  //   }
  // } else if (currentState == QdState::HANDSHAKE) {
  //   if (handshake()) {
  //     updateQDState(QdState::DUEL_ALERT);
  //   } else if (handshakeTimedOut) {
  //     updateQDState(QdState::ACTIVATED);
  //   }
  // } else if (currentState == QdState::DUEL_ALERT) {
  //   alertDuel();
  //   if (alertCount == 9) {
  //     updateQDState(QdState::DUEL_COUNTDOWN);
  //   }
  // } else if (currentState == QdState::DUEL_COUNTDOWN) {
  //   duelCountdown();
  //   if (doBattle) {
  //     updateQDState(QdState::DUEL);
  //   }
  // } else if (currentState == QdState::DUEL) {
  //   duel();
  //   if (captured) {
  //     updateQDState(QdState::LOSE);
  //   } else if (wonBattle) {
  //     updateQDState(QdState::WIN);
  //   } else if (duelTimedOut) {
  //     updateQDState(QdState::ACTIVATED);
  //   }
  // } else if (currentState == QdState::WIN) {
  //   if (!reset) {
  //     duelOver();
  //   } else {
  //     updateScore(true);
  //     PDN->clearComms();
  //     updateQDState(QdState::DORMANT);
  //   }
  // } else if (currentState == QdState::LOSE) {
  //   if (!reset) {
  //     duelOver();
  //   } else {
  //     updateScore(false);
  //     PDN->clearComms();
  //     updateQDState(QdState::DORMANT);
  //   }
  // }

  // commitQDState();
}

// void Quickdraw::setupActivation() {
//   if (playerInfo.isHunter()) {
//     // hunters have minimal activation delay
//     stateTimer.setTimer(5000);
//     //also go ahead and initialize the next match id here.
//     currentMatchId = generateUuid();
//   } else {
//     if (debugDelay > 0) {
//       stateTimer.setTimer(debugDelay);
//     } else {
//       long timer = random(bountyDelay[0], bountyDelay[1]);
//       stateTimer.setTimer(timer);
//     }
//   }
//
//   activationInitiated = true;
// }
//
// bool Quickdraw::shouldActivate() { return stateTimer.expired(); }
//
// bool Quickdraw::activationSequence() {
//   if (stateTimer.expired()) {
//     if (activateMotorCount < 19) {
//       if (activateMotor) {
//         // PDN->getVibrator().max();
//       } else {
//         // PDN->getVibrator().off();
//       }
//
//       stateTimer.setTimer(100);
//       activateMotorCount++;
//       activateMotor = !activateMotor;
//       return false;
//     } else {
//       activateMotorCount = 0;
//     //   PDN->getVibrator().off();
//       activateMotor = false;
//       return true;
//     }
//   }
//   return false;
// }
//
// void Quickdraw::activationIdle() {
//   // msgDelay was to prevent this from broadcasting every loop.
//   if (msgDelay == 0) {
//     if(playerInfo.isHunter())
//     {
//     //   writeGameComms(HUNTER_BATTLE_MESSAGE);
//     }
//     else
//     {
//     //   writeGameComms(BOUNTY_BATTLE_MESSAGE);
//     }
//   }
//   msgDelay = msgDelay + 1;
// }
//
// bool Quickdraw::initiateHandshake() {
//   bool isHunter = playerInfo.isHunter();
// //   if (gameCommsAvailable()) {
// //     if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && isHunter) {
// //       readGameComms();
// //       writeGameComms(HUNTER_BATTLE_MESSAGE);
// //       return true;
// //     } else if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !isHunter) {
// //       readGameComms();
// //       writeGameComms(BOUNTY_BATTLE_MESSAGE);
// //       return true;
// //     }
// //   }
//
//   return false;
// }
//
// // when this functions returns true, its the signal to change state
// bool Quickdraw::handshake() {
//   // dont transition gamestate, just handshake sub-fsm
//   if (handshakeState == HandshakeState::HANDSHAKE_TIMEOUT_START_STATE) {
//     stateTimer.setTimer(HANDSHAKE_TIMEOUT);
//     handshakeState = HandshakeState::HANDSHAKE_SEND_ROLE_SHAKE_STATE;
//   } else if (handshakeState == HandshakeState::HANDSHAKE_SEND_ROLE_SHAKE_STATE) {
//     if (stateTimer.expired()) {
//       handshakeTimedOut = true;
//       return false;
//     }
//
//     if (playerInfo.isHunter()) {
//       writeGameComms(HUNTER_SHAKE);
//     } else {
//       writeGameComms(BOUNTY_SHAKE);
//     }
//
//     handshakeState = HandshakeState::HANDSHAKE_WAIT_ROLE_SHAKE_STATE;
//   } else if(handshakeState == HandshakeState::HANDSHAKE_WAIT_ROLE_SHAKE_STATE) {
//     if(stateTimer.expired()) {
//       handshakeTimedOut = true;
//       return false;
//     }
//     //While waiting to see the shake, if we get battle message,
//     //then the other device is behind, we need to send it another
//     //battle message ack.
//     if(peekGameComms() == HUNTER_BATTLE_MESSAGE && !playerInfo.isHunter())
//     {
//       //it's confused.
//       while(peekGameComms() == HUNTER_BATTLE_MESSAGE) {
//         readGameComms();
//       }
//       writeGameComms(BOUNTY_BATTLE_MESSAGE);
//       writeGameComms(BOUNTY_SHAKE);
//     }
//
//     else if(peekGameComms() == BOUNTY_BATTLE_MESSAGE && playerInfo.isHunter())
//     {
//       //also confused.
//       while(peekGameComms() == BOUNTY_BATTLE_MESSAGE) {
//         readGameComms();
//       }
//       writeGameComms(HUNTER_BATTLE_MESSAGE);
//       writeGameComms(HUNTER_SHAKE);
//     }
//
//     if(peekGameComms() == BOUNTY_SHAKE && playerInfo.isHunter())
//     {
//       writeGameString(current_match_id);         // Sending match_id
//       writeGameString(playerInfo.getUserID());              // Sending userID
//       while(peekGameComms() == BOUNTY_SHAKE) {
//           readGameComms();
//       }
//       handshakeState = HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
//     }
//     else if(peekGameComms() == HUNTER_SHAKE && !playerInfo.isHunter())
//     {
//       writeGameString(playerInfo.getUserID());
//       while(peekGameComms() == HUNTER_SHAKE) {
//         readGameComms();
//       }
//       handshakeState = HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE;
//     }
//
//   } else if (handshakeState == HandshakeState::HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE) {
//     if (stateTimer.expired()) {
//       handshakeTimedOut = true;
//       return false;
//     }
//
//     if (playerInfo.isHunter())
//     {
//       if(gameCommsAvailable()) {
//         current_opponent_id = readGameString('\r');
//         writeGameComms(HUNTER_HANDSHAKE_FINAL_ACK);
//         handshakeState = HandshakeState::HANDSHAKE_STATE_FINAL_ACK;
//         return false;
//       }
//     }
//     else if (!playerInfo.isHunter())
//     {
//       if(gameCommsAvailable()) {
//         current_match_id    = readGameString('\r');
//         readGameString('\n');
//         current_opponent_id = readGameString('\r');
//         writeGameComms(BOUNTY_HANDSHAKE_FINAL_ACK);
//         handshakeState = HandshakeState::HANDSHAKE_STATE_FINAL_ACK;
//         return false;
//       }
//     }
//   } else if (handshakeState == HandshakeState::HANDSHAKE_STATE_FINAL_ACK) {
//     if (stateTimer.expired()) {
//       handshakeTimedOut = true;
//       return false;
//     }
//
//     if(!playerInfo.isHunter() && peekGameComms() == HUNTER_HANDSHAKE_FINAL_ACK) {
//       return true;
//     }
//
//     if(playerInfo.isHunter() && peekGameComms() == BOUNTY_HANDSHAKE_FINAL_ACK) {
//       return true;
//     }
//   }
//
//   return false;
// }
//
// void Quickdraw::alertDuel() {
//   if (alertCount == 0) {
//     buttonBrightness = 255;
//   }
//
//   if (stateTimer.expired()) {
//     if (buttonBrightness == 255) {
//       buttonBrightness = 0;
//     } else {
//       buttonBrightness = 255;
//     }
//
//     alertCount++;
//     FastLED.setBrightness(buttonBrightness);
//     stateTimer.setTimer(alertFlashTime);
//   }
// }
//
// void Quickdraw::duelCountdown() {
//   if (stateTimer.expired()) {
//     if (countdownStage == 4) {
//       FastLED.showColor(currentPalette[0], 255);
//       stateTimer.setTimer(FOUR);
//       displayIsDirty = true;
//       countdownStage = 3;
//     } else if (countdownStage == 3) {
//       FastLED.showColor(currentPalette[0], 150);
//       stateTimer.setTimer(THREE);
//       displayIsDirty = true;
//       countdownStage = 2;
//     } else if (countdownStage == 2) {
//       FastLED.showColor(currentPalette[0], 75);
//       stateTimer.setTimer(TWO);
//       displayIsDirty = true;
//       countdownStage = 1;
//     } else if (countdownStage == 1) {
//       FastLED.showColor(currentPalette[0], 0);
//       stateTimer.setTimer(ONE);
//       displayIsDirty = true;
//       countdownStage = 0;
//     } else if (countdownStage == 0) {
//       doBattle = true;
//     }
//   }
// }
//
// void Quickdraw::duel() {
//
//   if (peekGameComms() == ZAP) {
//     readGameComms();
//     writeGameComms(YOU_DEFEATED_ME);
//     captured = true;
//     return;
//   } else if (peekGameComms() == YOU_DEFEATED_ME) {
//     readGameComms();
//     wonBattle = true;
//     return;
//   } else {
//     readGameComms();
//   }
//
//   // primary.tick();
//
//   if (startDuelTimer) {
//     stateTimer.setTimer(DUEL_TIMEOUT);
//     startDuelTimer = false;
//   }
//
//   if (stateTimer.expired()) {
//     // FastLED.setBrightness(0);
//     bvbDuel = false;
//     duelTimedOut = true;
//   }
// }
//
// void Quickdraw::duelOver() {
//   if (startBattleFinish) {
//     startBattleFinish = false;
//     PDN->getVibrator().max();
//   }
//
//   if (stateTimer.expired()) {
//     stateTimer.setTimer(500);
//     if (finishBattleBlinkCount < FINISH_BLINKS) {
//       if (finishBattleBlinkCount % 2 == 0) {
//         PDN->getVibrator().off();
//       } else {
//         PDN->getVibrator().max();
//       }
//       finishBattleBlinkCount = finishBattleBlinkCount + 1;
//     } else {
//       PDN->getVibrator().off();
//       reset = true;
//     }
//   }
// }
//
// void Quickdraw::setPlayerInfo(Player player) {
//     playerInfo = player;
// }
//
// void Quickdraw::setPlayerInfo(String playerJson) {
//     playerInfo.fromJson(playerJson);
// }
//
// void Quickdraw::addMatch(String currentMatchId, String currentOpponentId) {
//   if (numMatches < MAX_MATCHES) {
//     // Create a Match object
//     numMatches++;
//     if(playerInfo.isHunter()) {
//       matches[numMatches].setupMatch(currentMatchId, playerInfo.getUserID(), currentOpponentId);
//     } else {
//       matches[numMatches].setupMatch(currentMatchId, currentOpponentId, playerInfo.getUserID());
//     }
//   }
// }
//
// void Quickdraw::updateScore(bool winnerIsHunter) {
//   matches[numMatches].setWinner(winnerIsHunter);
// }

