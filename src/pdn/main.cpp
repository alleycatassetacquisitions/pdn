#include <Arduino.h>

#include <WiFi.h>
#include <FastLED.h>
#include <Preferences.h>

#include "device/drivers/esp32-s3/esp32-s3-logger-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-clock-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-1-button-driver.hpp"
#include "device/drivers/esp32-s3/ws2812b-fastled-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-haptics-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-serial-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-http-client-driver.hpp"
#include "device/drivers/esp32-s3/esp-now-driver.hpp"
#include "device/drivers/esp32-s3/ssd1306-u8g2-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-prefs-driver.hpp"

#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include "state/state-machine.hpp"
#include "device/pdn.hpp"
#include "pdn-constants.hpp"
#include "game/quickdraw.hpp"
#include "id-generator.hpp"
#include "wireless/remote-player-manager.hpp"
#include "game/match-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "game/quickdraw-resources.hpp"
#include "wireless/resender.hpp"
#include "wireless/wireless-transport.hpp"
#include "device/remote-device-coordinator.hpp"

// WiFi configuration - injected at compile time from wifi_credentials.ini
// See wifi_credentials.ini.example for template
#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Please create wifi_credentials.ini from wifi_credentials.ini.example"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined. Please create wifi_credentials.ini from wifi_credentials.ini.example"
#endif
#ifndef BASE_URL
#error "BASE_URL not defined. Please create wifi_credentials.ini from wifi_credentials.ini.example"
#endif

WifiConfig* wifiConfig = nullptr;


// ESP32-s3 Drivers (declare as pointers, construct in setup())
Esp32S3Clock* clockDriver = nullptr;
SSD1306U8G2Driver* displayDriver = nullptr;
Esp32S31ButtonDriver* primaryButtonDriver = nullptr;
Esp32S31ButtonDriver* secondaryButtonDriver = nullptr;
WS2812BFastLEDDriver<displayLightsPin, gripLightsPin>* lightDriver = nullptr;
Esp32S3HapticsDriver* hapticsDriver = nullptr;
Esp32s3SerialJack* serialOutDriver = nullptr;
Esp32s3SerialJack* serialInDriver = nullptr;
Esp32S3HttpClient* httpClientDriver = nullptr;
EspNowDriver* peerCommsDriver = nullptr;
Esp32S3Logger* loggerDriver = nullptr;
Esp32S3PrefsDriver* storageDriver = nullptr;

// Core game objects (declare as pointers, construct in setup())
Device* pdn = nullptr;
Player* player = nullptr;

// Game instance
Quickdraw* game = nullptr;

// Remote player management
SymbolWirelessManager* symbolWirelessManager = nullptr;
RemoteDebugManager* remoteDebugManager = nullptr;
WirelessTransport* gWirelessTransport = nullptr;

void setupEspNow(
    RemoteDebugManager* remoteDebugManager,
    SymbolWirelessManager* symbolWirelessManager,
    PeerCommsInterface* peerCommsDriver) {
    // Reliable-channel PktTypes (kQuickdrawCommand, kChain*, kShootoutCommand)
    // and kAck are wired by WirelessTransport itself; only the non-channel
    // subsystems register here.
    peerCommsDriver->setPacketHandler(
        PktType::kDebugPacket,
        [](const uint8_t* srcAddr, const uint8_t* data, const size_t len, void* userArg) {
            ((RemoteDebugManager*)userArg)->ProcessDebugPacket(srcAddr, data, len);
        },
        remoteDebugManager
    );

    peerCommsDriver->setPacketHandler(
        PktType::kSymbolMatchCommand,
        [](const uint8_t* srcAddr, const uint8_t* data, const size_t len, void* userArg) {
            ((SymbolWirelessManager*)userArg)->processSymbolMatchCommand(srcAddr, data, len);
        },
        symbolWirelessManager
    );

}

// Drives connectivity (HELLO emit + silent-link watchdog) on a fixed 20ms
// cadence independent of the main loop. The main loop's jitter (HTTP, display
// render, ESP-NOW bursts) could otherwise delay the watchdog past its threshold
// and false-fire a disconnect, or starve HELLO emission so a peer thinks WE
// vanished. The receive side (HELLO/BEACON parsing) runs on the UART event
// tasks; this task and those feed RDC's lock-guarded recv queue, which the main
// loop drains in rdc->sync().
static void connectivityTask(void* arg) {
    auto* rdc = static_cast<RemoteDeviceCoordinator*>(arg);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        rdc->serviceConnectivity(millis());
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    // Construct drivers FIRST (before anything that might use logging or timers)
    loggerDriver = new Esp32S3Logger(LOGGER_DRIVER_NAME);
    clockDriver = new Esp32S3Clock(PLATFORM_CLOCK_DRIVER_NAME);
    
    // Initialize platform abstractions immediately after constructing them
    g_logger = loggerDriver;
    SimpleTimer::setPlatformClock(clockDriver);
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Now construct remaining drivers (safe to use logging and timers now)
    displayDriver = new SSD1306U8G2Driver(DISPLAY_DRIVER_NAME, displayCS, displayDC, displayRST);
    primaryButtonDriver = new Esp32S31ButtonDriver(PRIMARY_BUTTON_DRIVER_NAME, primaryButtonPin);
    secondaryButtonDriver = new Esp32S31ButtonDriver(SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonPin);
    lightDriver = new WS2812BFastLEDDriver<displayLightsPin, gripLightsPin>(LIGHT_DRIVER_NAME, numDisplayLights, numGripLights);
    hapticsDriver = new Esp32S3HapticsDriver(HAPTICS_DRIVER_NAME, motorPin);
    // OUTPUT jack receives on the cable TIP (worst TRS contact): pulldown.
    // INPUT jack's contact is solid: pullup. See Esp32s3SerialJack::RxPull.
    serialOutDriver = new Esp32s3SerialJack(SERIAL_OUT_DRIVER_NAME, Serial1, TXt, TXr,
                                            Esp32s3SerialJack::RxPull::Down);
    serialInDriver = new Esp32s3SerialJack(SERIAL_IN_DRIVER_NAME, Serial2, RXt, RXr,
                                           Esp32s3SerialJack::RxPull::Up);
    
    // WiFi credentials are compile-time constants from build flags
    wifiConfig = new WifiConfig(WIFI_SSID, WIFI_PASSWORD, BASE_URL);
    peerCommsDriver = EspNowDriver::CreateEspNowDriver(PEER_COMMS_DRIVER_NAME);
    httpClientDriver = new Esp32S3HttpClient(HTTP_CLIENT_DRIVER_NAME, wifiConfig);
    storageDriver = new Esp32S3PrefsDriver(STORAGE_DRIVER_NAME, PREF_NAMESPACE);

    // Create driver configuration
    DriverConfig pdnConfig = {
        {DISPLAY_DRIVER_NAME, displayDriver},
        {PRIMARY_BUTTON_DRIVER_NAME, primaryButtonDriver},
        {SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonDriver},
        {LIGHT_DRIVER_NAME, lightDriver},
        {HAPTICS_DRIVER_NAME, hapticsDriver},
        {SERIAL_OUT_DRIVER_NAME, serialOutDriver},
        {SERIAL_IN_DRIVER_NAME, serialInDriver},
        {HTTP_CLIENT_DRIVER_NAME, httpClientDriver},
        {PEER_COMMS_DRIVER_NAME, peerCommsDriver},
        {PLATFORM_CLOCK_DRIVER_NAME, clockDriver},
        {LOGGER_DRIVER_NAME, loggerDriver},
        {STORAGE_DRIVER_NAME, storageDriver},
    };

    // Create core game objects
    pdn = PDN::createPDN(pdnConfig);
    
    IdGenerator::initialize(clockDriver->milliseconds());
    player = new Player();
    player->setUserID(IdGenerator::getInstance().generateId());

    pdn->begin();
    // Create wireless managers
    LOG_I("SETUP", "Creating SymbolWirelessManager...");
    symbolWirelessManager = new SymbolWirelessManager();
    LOG_I("SETUP", "Creating RemoteDebugManager...");
    remoteDebugManager = new RemoteDebugManager(peerCommsDriver);
    
    // WiFi credentials are compile-time constants from build flags
    remoteDebugManager->Initialize(WIFI_SSID, WIFI_PASSWORD, BASE_URL);

    gWirelessTransport = new WirelessTransport(pdn->getWirelessManager());
    symbolWirelessManager->initialize(pdn->getWirelessManager(), pdn->getRemoteDeviceCoordinator());

    setupEspNow(remoteDebugManager, symbolWirelessManager, peerCommsDriver);
    
    game = new Quickdraw(player, pdn, remoteDebugManager, symbolWirelessManager, gWirelessTransport);
    
    pdn->getDisplay()->
    invalidateScreen()->
        drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))->
        drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))->
        render();
    delay(3000);

    // Register state machines with the device and launch Quickdraw
    AppConfig apps = {
        {StateId(QUICKDRAW_APP_ID), game}
    };
    pdn->loadAppConfig(apps, StateId(QUICKDRAW_APP_ID));

    // Hand connectivity timing to a dedicated task so it stops sharing the main
    // loop's jitter. setExternalConnectivityTask(true) makes rdc->sync() skip
    // its inline serviceConnectivity call (native tests keep driving it inline).
    // Priority 2 sits just above the Arduino loop (1) so the 20ms cadence holds
    // under load; pinned to APP_CPU to leave PRO_CPU for the WiFi/ESP-NOW stack.
    if (auto* rdc = pdn->getRemoteDeviceCoordinator()) {
        rdc->setExternalConnectivityTask(true);
        xTaskCreatePinnedToCore(connectivityTask, "pdn-conn", 4096, rdc, 2,
                                nullptr, APP_CPU_NUM);
    }
}

void loop() {
    pdn->loop();
    if (gWirelessTransport) gWirelessTransport->sync();
}
