#include "device/pdn.hpp"
#include "game/ping-queue.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "wireless/esp-now-comms.hpp"
#include <string>
#include <Arduino.h>
#include "player.hpp"
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
using namespace std;

void handlePrimaryButtonClick(void* context) {
    auto* idle = static_cast<Idle*>(context);
    idle->handlePrimaryButton();
}

void handleSecondaryButtonClick(void* context) {
    auto* idle = static_cast<Idle*>(context);
    idle->handleSecondaryButton();
}

Idle::Idle(PingQueue* pingQueue, Player* player) : State(QuickdrawStateId::IDLE), 
    pingQueue(pingQueue), player(player), displayCount(0),
    needsRender(false), sendMacAddress(false), 
    waitingForMacAddress(false), transitionToHandshakeState(false) {
}

Idle::~Idle() {
}

void Idle::handlePrimaryButton() {
    pingQueue->addPing(*player->getOpponentMacAddress());
    needsRender = true;
}

void Idle::handleSecondaryButton() {
    pingQueue->clear();
    displayCount = 0;
    needsRender = true;
}

void Idle::onStateMounted(Device* PDN) {
    // Set up button callbacks
    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::PRIMARY_BUTTON, handlePrimaryButtonClick, this);
    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::SECONDARY_BUTTON, handleSecondaryButtonClick, this);

    // Add 2 debug pings to the queue using opponent's MAC address
    pingQueue->addPing(*player->getOpponentMacAddress());
    pingQueue->addPing(*player->getOpponentMacAddress());
    displayCount = pingQueue->getValidPingCount(*player->getOpponentMacAddress());
    needsRender = true;

    // Set up serial callback
    PDN->setOnStringReceivedCallback(std::bind(&Idle::serialEventCallbacks, this, std::placeholders::_1));

    // Set up animation
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

void Idle::onStateLoop(Device* PDN) {
    static uint32_t lastCleanup = 0;
    static uint32_t lastHeartbeat = 0;
    uint32_t currentMillis = millis();

    // Clean up expired pings every second
    if (currentMillis - lastCleanup >= 1000) {
        lastCleanup = currentMillis;
        pingQueue->cleanup();
        int actualCount = pingQueue->getValidPingCount(*player->getOpponentMacAddress());
        if (actualCount != displayCount) {
            displayCount = actualCount;
            needsRender = true;
        }
    }

    // Only render when needed
    if (needsRender) {
        // Clear the screen and reset to default state
        PDN->invalidateScreen();
        PDN->setGlyphMode(FontMode::TEXT);
        
        // Draw the background image
        PDN->drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE));
        
        // Draw all text in inverted mode
        PDN->setGlyphMode(FontMode::TEXT_INVERTED);
        PDN->drawText("Hello,", 64 + 6, 20);
        PDN->drawText("Count: ", 64 + 6, 40);
        PDN->drawText(std::to_string(displayCount).c_str(), 64 + 6, 60);
        
        // Reset to default mode and render
        PDN->setGlyphMode(FontMode::TEXT);
        PDN->render();
        needsRender = false;
    }

    // Handle heartbeat for non-hunters
    if (!player->isHunter() && currentMillis - lastHeartbeat >= 250) {
        lastHeartbeat = currentMillis;
        PDN->writeString(SERIAL_HEARTBEAT.c_str());
    }

    // Handle MAC address sending
    if(sendMacAddress) {
        uint8_t macAddr[6];
        esp_read_mac(macAddr, ESP_MAC_WIFI_STA);
        const char* macStr = MacToString(macAddr);
        ESP_LOGI("IDLE", "Preparing to Send Mac Address: %s", macStr);
        
        PDN->writeString(SEND_MAC_ADDRESS);
        PDN->writeString(macStr);
        transitionToHandshakeState = true;
    }
}

void Idle::onStateDismounted(Device* PDN) {
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
    
    // Reset state variables
    transitionToHandshakeState = false;
    sendMacAddress = false;
    waitingForMacAddress = false;
    needsRender = false;
    displayCount = 0;
    
    // Clear the queue
    pingQueue->clear();
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
