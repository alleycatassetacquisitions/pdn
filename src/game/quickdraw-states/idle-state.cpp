#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "wireless/esp-now-comms.hpp"
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
Idle::Idle(Player* player) : State(IDLE) {
    this->player = player;
}

Idle::~Idle() {
    player = nullptr;
}

void Idle::onStateMounted(Device *PDN) {

    // cursor position is bottom left of the character drawn, need to start at y = 16 otherwise we will be cut off screen
    // 0, 0 is the top left pixel

    //starting at 64, you start in the white space for text


    //COUNT is the max character i can handle. 
    //X = 64 + 4 for min
    //Y = 20 for each line
    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    setGlyphMode(FontMode::TEXT_INVERTED)-> drawText("Hello,", 64 + 6, 20)->
    setGlyphMode(FontMode::TEXT_INVERTED)-> drawText("Count: ", 64 + 6, 40)->
    render();

    PDN->setOnStringReceivedCallback(std::bind(&Idle::serialEventCallbacks, this, std::placeholders::_1));

    AnimationConfig config;
    
    if(player->isHunter()) {
        config.type = AnimationType::IDLE;
        config.speed = 16;
        config.curve = EaseCurve::LINEAR;
        config.initialState = HUNTER_IDLE_STATE_ALTERNATE;
        config.loopDelayMs = 0;
        config.loop = true;
    } else {
        config.type = AnimationType::VERTICAL_CHASE;
        config.speed = 5;
        config.curve = EaseCurve::ELASTIC;
        config.initialState = BOUNTY_IDLE_STATE;
        config.loopDelayMs = 1500;
        config.loop = true;
    }
    PDN->startAnimation(config);
}

void Idle::onStateLoop(Device *PDN) {

    EVERY_N_MILLIS(250) {
        if(!player->isHunter()) {
            PDN->writeString(SERIAL_HEARTBEAT.c_str());
        }
    }

    if(sendMacAddress) {
        uint8_t macAddr[6];
        esp_read_mac(macAddr, ESP_MAC_WIFI_STA);
        const char* macStr = MacToString(macAddr);
        ESP_LOGI("IDLE", "Perparing to Send Mac Address: %s", macStr);
        
        PDN->writeString(SEND_MAC_ADDRESS);
        PDN->writeString(macStr);
        transitionToHandshakeState = true;
    }
}

void Idle::onStateDismounted(Device *PDN) {
    transitionToHandshakeState = false;
    sendMacAddress = false;
    waitingForMacAddress = false;
    PDN->clearCallbacks();
}

void Idle::serialEventCallbacks(string message) {
    ESP_LOGI("IDLE", "Serial event received: %s", message.c_str());
    if(message.compare(SERIAL_HEARTBEAT) == 0) {
        sendMacAddress = true;  
    } else if(message.compare(SEND_MAC_ADDRESS) == 0) {
        waitingForMacAddress = true;
    } else if(waitingForMacAddress) {
        waitingForMacAddress = false;
        player->setOpponentMacAddress(message);
        transitionToHandshakeState = true;
    }
}

bool Idle::transitionToHandshake() {
    return transitionToHandshakeState;
}
