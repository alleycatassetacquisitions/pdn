#pragma once

#include "device/wireless-manager.hpp"
#include "device/drivers/button.hpp"
#include "apps/fdn-connect/fdn-constants.hpp"
#include "game/player.hpp"
#include <functional>
#include <cstdint>

struct FdnConnectPacket {
    uint8_t command;
    char    playerId[PLAYER_ID_BUFFER_SIZE];
    uint8_t sequence[FDN_HACK_SEQUENCE_LENGTH];
    uint8_t buttonValue;
} __attribute__((packed));

enum FdnConnectCmd : uint8_t {
    PDN_CONNECT        = 0,
    SEND_HACK_SEQUENCE = 1,
    BUTTON_PRESS       = 2,
};

class FDNConnectWirelessManager {
public:
    FDNConnectWirelessManager();
    ~FDNConnectWirelessManager();

    void initialize(WirelessManager* wirelessManager);

    // PDN → FDN: announce jack insertion with the player's 4-digit ID
    int sendPdnConnect(const uint8_t macAddress[6], const char playerId[PLAYER_ID_BUFFER_SIZE]);

    // PDN → FDN: send one button press during the hacking sequence
    int sendButtonPress(const uint8_t macAddress[6], ButtonIdentifier button);

    // FDN → PDN: register handler for SEND_HACK_SEQUENCE
    void setHackSequenceCallback(const std::function<void(const uint8_t sequence[FDN_HACK_SEQUENCE_LENGTH])>& callback);

    void clearCallbacks();

    int processFDNConnectCommand(const uint8_t* src, const uint8_t* data, size_t len);

private:
    WirelessManager* wirelessManager = nullptr;
    std::function<void(const uint8_t sequence[FDN_HACK_SEQUENCE_LENGTH])> hackSequenceCallback;
};
