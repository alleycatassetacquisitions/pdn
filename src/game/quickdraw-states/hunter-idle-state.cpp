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
IdleHunter::IdleHunter(Player* player, QuickdrawWirelessManager* qwm) : State(IDLE_HUNTER) {
    this->player = player;
    this->wirelessManager = qwm;

    std::vector<const string *> writing;
    std::vector<const string *> reading;

    if (player->isHunter()) {
        reading.push_back(&BOUNTY_BATTLE_MESSAGE);
        writing.push_back(&HUNTER_BATTLE_MESSAGE);
    } else {
        reading.push_back(&HUNTER_BATTLE_MESSAGE);
        writing.push_back(&BOUNTY_BATTLE_MESSAGE);
    }

    registerValidMessages(reading);
    registerResponseMessage(writing);
}

IdleHunter::~IdleHunter() {
    player = nullptr;
}

void IdleHunter::onStateMounted(Device *PDN) {

    if (player->isHunter()) {
        currentPalette = hunterColors;
        wirelessManager->initialize(player, lockdownBroadcastDelay);
    } else {
        wirelessManager->initialize(player, hackBroadcastDelay);
        currentPalette = bountyColors;
    }

    wirelessManager->setPacketReceivedCallback(
        std::bind(&IdleHunter::onQuickdrawPacketReceived, this, std::placeholders::_1));

    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    render();

    setupButtonCallbacks(PDN);
}

void IdleHunter::onQuickdrawPacketReceived(QuickdrawCommand packet) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             packet.wifiMacAddr[0], packet.wifiMacAddr[1], packet.wifiMacAddr[2],
             packet.wifiMacAddr[3], packet.wifiMacAddr[4], packet.wifiMacAddr[5]);

    switch(packet.command) {
        case HACK: {
            if(player->isHunter()) {
                ESP_LOGI("IDLE", "Received HACK from %s - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
                updateHackProgress();
                // Broadcast HACK_ACK
                wirelessManager->broadcastPacket(HACK_ACK, 0, packet.ackCount+1);
            } else {
                ESP_LOGE("IDLE", "Received HACK from %s as Bounty - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
            }
            break;
        case HACK_ACK:
            if(!player->isHunter()) {
                ESP_LOGI("IDLE", "Received HACK_ACK from %s - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
                *hackAcks = packet.ackCount;
            } else {
                ESP_LOGE("IDLE", "Received HACK_ACK from %s as Bounty - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
            }
            break;
        case LOCKDOWN:
            if(!player->isHunter()) {
                ESP_LOGI("IDLE", "Received LOCKDOWN from %s - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
                updateLockdownProgress();
                // Broadcast LOCKDOWN_ACK
                wirelessManager->broadcastPacket(LOCKDOWN_ACK, 0, packet.ackCount+1);
            } else {
                ESP_LOGE("IDLE", "Received LOCKDOWN from %s as Hunter - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
            }
            break;
        case LOCKDOWN_ACK:
            if(player->isHunter()) {
                ESP_LOGI("IDLE", "Received LOCKDOWN_ACK from %s - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
                *lockdownAcks = packet.ackCount;
            } else {
                ESP_LOGE("IDLE", "Received LOCKDOWN_ACK from %s as Bounty - ackCount: %d, drawTime: %ld ms", 
                         macStr, packet.ackCount, packet.drawTimeMs);
            }
            break;
        default:
            ESP_LOGE("IDLE", "Unhandled command %02x from %s - ackCount: %d, drawTime: %ld ms", 
                     packet.command, macStr, packet.ackCount, packet.drawTimeMs);
    }
    }
}


void IdleHunter::onStateLoop(Device *PDN) {
    commandTimeout.updateTime();
    if(commandTimeout.expired()) {
        wirelessManager->clearPackets();
    }
    // EVERY_N_MILLIS(250) {
    //     PDN->writeString(&responseStringMessages[0]);
    // }

    EVERY_N_MILLIS(16) {
        ledAnimation(PDN);
    }

    // Update LEDs if we have new packet data
    if(needsLEDUpdate) {
        if(player->isHunter()) {
            PDN->setLEDBarLeft(LOCKDOWN_THRESHOLD-*lockdownAcks);
            PDN->setLEDBarRight(LOCKDOWN_THRESHOLD-*lockdownAcks);
        } else {
            PDN->setLEDBarLeft(HACK_THRESHOLD-*hackAcks);
            PDN->setLEDBarRight(HACK_THRESHOLD-*hackAcks);
        }
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

void IdleHunter::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    // PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);

    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

void IdleHunter::ledAnimation(Device *PDN) {
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

void IdleHunter::updateHackProgress() {
    if(*hackAcks > HACK_THRESHOLD) {
        transitionToHackedState = true;
        ESP_LOGI("IDLE", "Hack threshold reached!");
    } else {
        ESP_LOGI("IDLE", "Hack progress: %d", *hackAcks);
        needsLEDUpdate = true;  // Flag for LED update
    }
}

void IdleHunter::updateLockdownProgress() {
    if(*lockdownAcks > LOCKDOWN_THRESHOLD) {
        transitionToLockdownState = true;
        ESP_LOGI("IDLE", "Lockdown threshold reached!");
    } else {
        ESP_LOGI("IDLE", "Lockdown progress: %d", *lockdownAcks);
        needsLEDUpdate = true;  // Flag for LED update
    }
}

void IdleHunter::setupButtonCallbacks(Device* PDN) {
    if(player->isHunter()) {
        PDN->setButtonClick(
            ButtonInteraction::DURING_LONG_PRESS,
            ButtonIdentifier::PRIMARY_BUTTON,
            [] {
                QuickdrawWirelessManager* qwm = QuickdrawWirelessManager::GetInstance();
                qwm->broadcastPacket(LOCKDOWN, 0, qwm->getPacketAckCount(LOCKDOWN_ACK));
        });
    } else {
        PDN->setButtonClick(
            ButtonInteraction::DURING_LONG_PRESS,
            ButtonIdentifier::PRIMARY_BUTTON,
            [] {
                QuickdrawWirelessManager* qwm = QuickdrawWirelessManager::GetInstance();
                qwm->broadcastPacket(HACK, 0, qwm->getPacketAckCount(HACK_ACK));
        });
    }
}


bool IdleHunter::transitionToHandshake() {
    return transitionToHandshakeState;
}

bool IdleHunter::transitionToLockdown() {
    return transitionToLockdownState;
}

bool IdleHunter::transitionToHacked() {
    return transitionToHackedState;
}
