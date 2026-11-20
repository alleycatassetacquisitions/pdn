#pragma once

#include <string>

#include "alleycat-server/fdn-record-store.hpp"
#include "alleycat-server/fdn-record-uploader.hpp"
#include "apps/player-fetch/player-fetch.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-display-driver.hpp"
#include "device/drivers/native/native-haptics-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/pdn.hpp"
#include "fdn/device/fdn.hpp"
#include "game/game-session.hpp"
#include "game/player-provider.hpp"
#include "game/quickdraw-apps.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"

#include "sim-http.hpp"
#include "sim-medium.hpp"
#include "sim-nvs.hpp"

namespace soak {

/** Who a PDN's player is: the server-registered ids it carries. */
struct PdnIdentity {
    UUIDBytes playerId;
    Team team;
};

// One PDN built as DeviceFactory::createDevice builds it (src/cli/cli-device.hpp),
// with the harness radio, flash and identity in place of the CLI's broker ones.
class SimPdn {
public:
    /** Builds and boots the PDN; call with its clock installed. */
    SimPdn(const std::string& suffix, VirtualMedium& medium, const uint8_t* mac, SimNvs& nvs,
           const PdnIdentity& identity)
        : primaryButton(new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0))
        , serialOut(new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix))
        , serialIn(new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix)) {
        DriverConfig config = {
        {DISPLAY_DRIVER_NAME, new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix)},
        {PRIMARY_BUTTON_DRIVER_NAME, primaryButton},
        {SECONDARY_BUTTON_DRIVER_NAME,
         new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1)},
        {LIGHT_DRIVER_NAME, new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix)},
        {HAPTICS_DRIVER_NAME, new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0)},
        {SERIAL_OUT_DRIVER_NAME, serialOut},
        {SERIAL_IN_DRIVER_NAME, serialIn},
        {PEER_COMMS_DRIVER_NAME, new SimRadio(PEER_COMMS_DRIVER_NAME + suffix, medium, mac)},
        {STORAGE_DRIVER_NAME,
         new SimNvsDriver(STORAGE_DRIVER_NAME + suffix, nvs, {MATCH_RING_PREFS_NAMESPACE})},
        };
        firmware = PDN::createPDN(config);
        firmware->begin();
        playerProvider = new PlayerProvider();
        player = playerProvider->getPlayer();
        player->applyRegistrationProfile(identity.playerId, "soak", identity.team, {});
        player->setAllegiance(Allegiance::RESISTANCE);
        symbolWirelessManager = new SymbolWirelessManager(firmware->getWirelessManager(),
                                                          firmware->getRemoteDeviceCoordinator());
        gameSession = new GameSession(player, firmware, symbolWirelessManager);
        GameContext context = gameSession->getContext();
        playerFetchApp =
        new PlayerFetchApp(firmware->getFirmwareUpdateManager(), playerProvider, true);
        hubApp = new HubApp(context);
        matchApp = new MatchApp(context);
        shootoutApp = new ShootoutApp(context);
        symbolApp = new SymbolApp(context);
        firmware->loadAppConfig({{StateId(PLAYER_FETCH_APP_ID), playerFetchApp},
                                 {StateId(HUB_APP_ID), hubApp},
                                 {StateId(MATCH_APP_ID), matchApp},
                                 {StateId(SHOOTOUT_APP_ID), shootoutApp},
                                 {StateId(SYMBOL_APP_ID), symbolApp}},
                                StateId(HUB_APP_ID));
    }

    /** Tears down in DeviceFactory::destroyDevice order; call with its clock installed. */
    ~SimPdn() {
        delete playerFetchApp;
        delete hubApp;
        delete matchApp;
        delete shootoutApp;
        delete symbolApp;
        delete gameSession;
        delete symbolWirelessManager;
        delete playerProvider;
        delete firmware;
    }

    /** The jack's serial driver. */
    NativeSerialDriver* jack(SerialIdentifier which) const {
        return which == SerialIdentifier::OUTPUT_JACK ? serialOut : serialIn;
    }

    /** The running state's id, or -1. */
    int stateId() const {
        StateMachine* app = firmware->getActiveApp();
        State* state = app != nullptr ? app->getCurrentState() : nullptr;
        return state != nullptr ? state->getStateId() : -1;
    }

    /** The managers the apps share. */
    GameContext context() const { return gameSession->getContext(); }

    /** Presses, as a thumb on the primary button. */
    void press() const { primaryButton->execCallback(ButtonInteraction::PRESS); }

    /** Clicks the primary button. */
    void click() const { primaryButton->execCallback(ButtonInteraction::CLICK); }

    PDN* firmware = nullptr;

private:
    NativeButtonDriver* primaryButton = nullptr;
    NativeSerialDriver* serialOut = nullptr;
    NativeSerialDriver* serialIn = nullptr;
    PlayerProvider* playerProvider = nullptr;
    Player* player = nullptr;
    SymbolWirelessManager* symbolWirelessManager = nullptr;
    GameSession* gameSession = nullptr;
    PlayerFetchApp* playerFetchApp = nullptr;
    HubApp* hubApp = nullptr;
    MatchApp* matchApp = nullptr;
    ShootoutApp* shootoutApp = nullptr;
    SymbolApp* symbolApp = nullptr;
};

// One FDN: the real FDN device (two input jacks, an FDN-typed RDC that hands a
// cabled PDN its clock) with the record wiring of src/fdn/main.cpp:180-211 —
// store, uploader, 12 s beacon — and no apps, which the records path never reads.
class SimFdn {
public:
    static constexpr unsigned long BEACON_INTERVAL_MS = 12000;

    /** Builds and boots the FDN; call with its clock installed. */
    SimFdn(const std::string& suffix, VirtualMedium& medium, const uint8_t* mac, SimNvs& nvs,
           HttpBackend& backend, HttpFaults& faults, std::mt19937_64& rng)
        : serialIn(new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix))
        , serialInSecondary(new NativeSerialDriver(SERIAL_IN_SECONDARY_DRIVER_NAME + suffix)) {
        SimRadio* radio = new SimRadio(PEER_COMMS_DRIVER_NAME + suffix, medium, mac);
        SimNvsDriver* storage =
        new SimNvsDriver(STORAGE_DRIVER_NAME + suffix, nvs, {FDN_RECORD_STORE_NAMESPACE});
        DriverConfig config = {
        {LIGHT_DRIVER_NAME, new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix)},
        {SERIAL_IN_DRIVER_NAME, serialIn},
        {SERIAL_IN_SECONDARY_DRIVER_NAME, serialInSecondary},
        {HTTP_CLIENT_DRIVER_NAME,
         new SimHttpClient(HTTP_CLIENT_DRIVER_NAME + suffix, backend, faults, rng)},
        {PEER_COMMS_DRIVER_NAME, radio},
        {STORAGE_DRIVER_NAME, storage},
        };
        firmware = FDN::createFDN(config);
        firmware->begin();
        beacon = new RemotePlayerManager(radio);
        store = new FdnRecordStore(firmware->getWirelessManager(), storage);
        RemoteDeviceCoordinator* rdc = firmware->getRemoteDeviceCoordinator();
        uploader = new FdnRecordUploader(firmware->getWirelessManager(), store, [rdc]() {
            return rdc->getJackStatus(SerialIdentifier::INPUT_JACK) != JackStatus::DISCONNECTED ||
                   rdc->getJackStatus(SerialIdentifier::INPUT_JACK_SECONDARY) !=
                   JackStatus::DISCONNECTED;
        });
        firmware->setTickCallback([this]() {
            beacon->Update();
            store->sync();
            uploader->sync();
        });
        beacon->StartBroadcastingPlayerInfo(&player, BEACON_INTERVAL_MS, DeviceType::FDN);
    }

    /** Frees what the constructor built. */
    ~SimFdn() {
        delete uploader;
        delete store;
        delete beacon;
        delete firmware;
    }

    /** INPUT_JACK or INPUT_JACK_SECONDARY. */
    NativeSerialDriver* jack(SerialIdentifier which) const {
        return which == SerialIdentifier::INPUT_JACK_SECONDARY ? serialInSecondary : serialIn;
    }

    FDN* firmware = nullptr;

private:
    NativeSerialDriver* serialIn = nullptr;
    NativeSerialDriver* serialInSecondary = nullptr;
    Player player;
    RemotePlayerManager* beacon = nullptr;
    FdnRecordStore* store = nullptr;
    FdnRecordUploader* uploader = nullptr;
};

}  // namespace soak
