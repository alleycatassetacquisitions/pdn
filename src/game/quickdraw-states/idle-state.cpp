#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "wireless/esp-now-comms.hpp"
#include "game/ping-queue.hpp"
#include "device-constants.hpp"
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

// Temporary test flag - set to false when done testing
#define TEST_PING_QUEUE true

Idle::Idle(Player* player) : State(IDLE) {
    this->player = player;
}

Idle::~Idle() {
    player = nullptr;
}

void Idle::onStateMounted(Device *PDN) {
    PDN->
    invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    render();

    PDN->setOnStringReceivedCallback(std::bind(&Idle::serialEventCallbacks, this, std::placeholders::_1));

    // Set up button callbacks
    if (TEST_PING_QUEUE) {
        // Top button adds pings
        PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::PRIMARY_BUTTON, 
            [](void* param) {
                Idle* state = static_cast<Idle*>(param);
                // Add a test ping to the queue
                PingQueue::Ping testPing;
                testPing.timestamp = millis();
                testPing.sourceId = state->getPlayer()->getUserID();
                state->getPingQueue().addPing(state->getPlayer()->getUserID());
                
                // Log the test ping
                Serial.printf("Test ping added to queue. Current valid count: %d\n", 
                    state->getPingQueue().getValidPingCount(state->getPlayer()->getUserID()));
            }, this);

        // Bottom button removes pings and sets shorter cooldown
        PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::SECONDARY_BUTTON,
            [](void* param) {
                Idle* state = static_cast<Idle*>(param);
                // Clear all pings
                state->getPingQueue().clear();
                // Log the cleared queue
                Serial.printf("Ping queue cleared. Current valid count: %d\n",
                    state->getPingQueue().getValidPingCount(state->getPlayer()->getUserID()));
            }, this);
    }
    
    // Set up animation based on player type
    AnimationConfig config;
    config.type = player->isHunter() ? AnimationType::IDLE : AnimationType::VERTICAL_CHASE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.loop = true;
    config.loopDelayMs = 0;
    
    // Set initial state based on player type
    config.initialState = player->isHunter() ? HUNTER_IDLE_STATE : BOUNTY_IDLE_STATE;
    
    // Start the animation
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

    // Update LED status based on PingQueue
    updateLEDStatus(PDN);
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

#define BOTTOM_RIGHT_DISPLAY_LED_INDEX 3
#define TOP_RIGHT_DISPLAY_LED_INDEX 8
#define TOTAL_LEDS_RIGHT_DISPLAY 6

void Idle::updateLEDStatus(Device* PDN) {
    // Get the number of valid pings for the current player
    int validPings = std::min(pingQueue.getValidPingCount(player->getUserID()), TOTAL_LEDS_RIGHT_DISPLAY);
    
    // Only modify LEDs if there are active pings
    if (validPings > 0) {
        // bottom of display LEDs right of display 3 -> 8 (3 is bot)
        for (int i = BOTTOM_RIGHT_DISPLAY_LED_INDEX; i <= TOP_RIGHT_DISPLAY_LED_INDEX; i++) {
            PDN->setLight(LightIdentifier::RIGHT_LIGHTS, i, LEDColor(0, 0, 0), LED_DIM_BRIGHTNESS, false);
        }
        
        // Set active pings to purple (starting from bottom)
        for (int i = 0; i < validPings; i++) {
            ESP_LOGI("IDLE", "Setting LED %d to purple", i + BOTTOM_RIGHT_DISPLAY_LED_INDEX);
            // Mark these LEDs as reserved so they won't be modified by animations
            PDN->setLight(LightIdentifier::RIGHT_LIGHTS, i + BOTTOM_RIGHT_DISPLAY_LED_INDEX, LEDColor(255, 0, 255), LED_ACTIVE_BRIGHTNESS, true); // Purple
        }
    }
}
