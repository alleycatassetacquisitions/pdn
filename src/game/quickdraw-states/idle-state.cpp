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
Idle::Idle(Player* player, QuickdrawWirelessManager* qwm) : State(IDLE) {
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

Idle::~Idle() {
    player = nullptr;
}

void Idle::onStateMounted(Device *PDN) {

    if (player->isHunter()) {
        currentPalette = hunterColors;
        wirelessManager->initialize(player, lockdownBroadcastDelay);
    } else {
        wirelessManager->initialize(player, hackBroadcastDelay);
        currentPalette = bountyColors;
    }

    wirelessManager->setPacketReceivedCallback(
        std::bind(&Idle::onQuickdrawPacketReceived, this, std::placeholders::_1));

    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    render();

    setupButtonCallbacks(PDN);
}

void Idle::onQuickdrawPacketReceived(QuickdrawCommand packet) {
    switch(packet.command) {
        case HACK: {
            if(player->isHunter()) {
                updateHackProgress();
            } else {
                ESP_LOGE("IDLE", "Received %02x as Bounty", packet.command);
            }
            break;
        }
        case LOCKDOWN:
            if(!player->isHunter()) {
                updateLockdownProgress();
            } else {
                ESP_LOGE("IDLE", "Received %02x as Hunter", packet.command);
            }
            break;
        default:
            ESP_LOGE("IDLE", "Unhandled command %02x", packet.command);
    }
}


void Idle::onStateLoop(Device *PDN) {
    commandTimeout.updateTime();
    if(commandTimeout.expired()) {
        wirelessManager->clearPackets();
    }
    // EVERY_N_MILLIS(250) {
    //     PDN->writeString(&responseStringMessages[0]);
    // }

    if(hackAcks > 0) {
        PDN->setLEDBarLeft(HACK_THRESHOLD-hackAcks);
        PDN->setLEDBarRight(HACK_THRESHOLD-hackAcks);
    }

    EVERY_N_MILLIS(16) {
        ledAnimation(PDN);
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

void Idle::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    // PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);

    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

void Idle::ledAnimation(Device *PDN) {
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

void Idle::updateHackProgress() {
    hackAcks = wirelessManager->getPacketAckCount(HACK);
    if(hackAcks > HACK_THRESHOLD) {
        //hacked
    } else {
        ESP_LOGI("IDLE", "Hack progress: %d", hackAcks);

        wirelessManager->broadcastPacket(HACK_ACK, 0, hackAcks);
    }
}

void Idle::updateLockdownProgress() {

}

void Idle::setupButtonCallbacks(Device* PDN) {
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


bool Idle::transitionToHandshake() {
    return transitionToHandshakeState;
}

bool Idle::transitionToLockdown() {
    return transitionToLockdownState;
}
