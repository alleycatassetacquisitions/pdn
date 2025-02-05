#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "wireless/remote-player-manager.hpp"
//
// Created by Elli Furedy on 9/30/2024.
//

/*
    void activationIdle()
    {
      // msgDelay was to prevent this from broadcasting every loop.
      if(msgDelay == 0) {
        comms().write(BATTLE_MESSAGE);
      }
      msgDelay = msgDelay + 1;
    }
 */
IdleBounty::IdleBounty(Player* player, QuickdrawWirelessManager* qwm) : State(IDLE_BOUNTY) {
    this->player = player;
    this->wirelessManager = qwm;


    std::vector<const string *> writing;
    std::vector<const string *> reading;

    reading.push_back(&HUNTER_BATTLE_MESSAGE);
    writing.push_back(&BOUNTY_BATTLE_MESSAGE);

    registerValidMessages(reading);
    registerResponseMessage(writing);
}

IdleBounty::~IdleBounty() {
    player = nullptr;
}


void IdleBounty::onStateMounted(Device *PDN) {


    
    wirelessManager->initialize(player, hackBroadcastDelay);
    currentPalette = bountyColors;

    wirelessManager->setPacketReceivedCallback(
        std::bind(&IdleBounty::onQuickdrawPacketReceived, this, std::placeholders::_1));

    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    render();

    setupButtonCallbacks(PDN);
}

void IdleBounty::onQuickdrawPacketReceived(QuickdrawCommand packet) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",

             packet.wifiMacAddr[0], packet.wifiMacAddr[1], packet.wifiMacAddr[2],
             packet.wifiMacAddr[3], packet.wifiMacAddr[4], packet.wifiMacAddr[5]);

    switch(packet.command) {
        case HACK_ACK: {
                ESP_LOGI("IDLE", "Received HACK_ACK from %s - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
                break;
        }
        case LOCKDOWN: {
            ESP_LOGI("IDLE", "Received LOCKDOWN from %s - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
                updateLockdownProgress();
                // Broadcast LOCKDOWN_ACK
                wirelessManager->broadcastPacket(LOCKDOWN_ACK, 0, packet.ackCount+1);
            break;
        }
        
        // error cases - we should not receive these signals as a bounty.
        case HACK: {
            ESP_LOGE("IDLE", "Received HACK from %s as Bounty - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
            break;
        }
        case LOCKDOWN_ACK: {
            ESP_LOGE("IDLE", "Received LOCKDOWN_ACK from %s as Bounty - ackCount: %d, drawTime: %ld ms", 
                        macStr, packet.ackCount, packet.drawTimeMs);
                
            break;
            }
        default:
            ESP_LOGE("IDLE", "Unhandled command %02x from %s - ackCount: %d, drawTime: %ld ms", 
                     packet.command, macStr, packet.ackCount, packet.drawTimeMs);
    }
}



void IdleBounty::onStateLoop(Device *PDN) {
    commandTimeout.updateTime();
    if(commandTimeout.expired()) {

        wirelessManager->clearPacket(HACK_ACK);
    }
    // EVERY_N_MILLIS(250) {
    //     PDN->writeString(&responseStringMessages[0]);
    // }

    EVERY_N_MILLIS(16) {
        ledAnimation(PDN);
    }

    // Update LEDs if we have new packet data
    if(needsLEDUpdate) {
        PDN->setLEDBarLeft(LOCKDOWN_THRESHOLD-*lockdownAcks);
        PDN->setLEDBarRight(LOCKDOWN_THRESHOLD-*lockdownAcks);
        needsLEDUpdate = false;  // Reset the flag
    }

    // // string *validMessage = waitForValidMessage(PDN);
    // // if (validMessage != nullptr) {
    // //     transitionToHandshakeState = true;
    // // }
    // EVERY_N_SECONDS(3) {
    //     if(playerManager->hasRemotePings()) {
    //         ESP_LOGI("PDN", "Seeing remote pings.\n");
    //         if(playerManager->getTopPingedByPlayer().numPings > 6) {
    //             transitionToLockdownState = true;
    //         }
    //     }
    // }

}

void IdleBounty::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);

    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

void IdleBounty::ledAnimation(Device *PDN) {
    if (breatheUp) {
        ledBrightness++;

    } else {
        ledBrightness--;
    }
    pwm_val = 255.0 * (1.0 - abs((2.0 * (ledBrightness / smoothingPoints)) - 1.0));

    if (ledBrightness == 255) {
        breatheUp = false;
    } else if (ledBrightness == 80) {
        breatheUp = true;
    }

    if (random8() % 8 == 0) {
        CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
        PDN->addToLight(LightIdentifier::DISPLAY_LIGHTS,
                        random8() % (numDisplayLights - 1),
                        LEDColor(color.r, color.g, color.b)
        );
    }


    for (int i = 0; i < numGripLights; i++) {
        if (random8() % 130 == 0) {
            CRGB color = ColorFromPalette(currentPalette, random8(), pwm_val, LINEARBLEND);
            PDN->addToLight(LightIdentifier::GRIP_LIGHTS,
                            i,
                            LEDColor(color.r, color.g, color.b)
            );
        }
    }

    if(random8() % 2) {
        PDN->fadeLightsBy(LightIdentifier::DISPLAY_LIGHTS, 1);
        PDN->fadeLightsBy(LightIdentifier::GRIP_LIGHTS, 1);
    }
}

void IdleBounty::updateLockdownProgress() {
    if(wirelessManager->getPacketAckCount(LOCKDOWN) > LOCKDOWN_THRESHOLD) {
        transitionToLockdownState = true;

        ESP_LOGI("IDLE", "Lockdown threshold reached!");
    } else {
        ESP_LOGI("IDLE", "Lockdown progress: %d", wirelessManager->getPacketAckCount(LOCKDOWN));
        needsLEDUpdate = true;  // Flag for LED update
    }
}

void IdleBounty::setupButtonCallbacks(Device* PDN) {
    
    PDN->setButtonClick(
        ButtonInteraction::DURING_LONG_PRESS,
        ButtonIdentifier::PRIMARY_BUTTON,
        [] {
            QuickdrawWirelessManager* qwm = QuickdrawWirelessManager::GetInstance();
            qwm->broadcastPacket(HACK, 0, qwm->getPacketAckCount(HACK_ACK));
    });
}


bool IdleBounty::transitionToHandshake() {
    return transitionToHandshakeState;
}


bool IdleBounty::transitionToLockdown() {
    return transitionToLockdownState;
}


bool IdleBounty::transitionToHacked() {
    return transitionToHackedState;
}