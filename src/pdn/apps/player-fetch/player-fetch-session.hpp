#pragma once

#include "alleycat-server/device-player.hpp"

/** Registration profile for the current PlayerFetch session. */
class PlayerFetchSession {
public:
    /** @return true when a device player profile is stored for this session. */
    bool hasDevicePlayer() const { return hasProfile; }

    /** @return the stored device player profile. */
    const DevicePlayer& getDevicePlayer() const { return devicePlayer; }

    /** Replaces the session profile with @p player. */
    void setDevicePlayer(const DevicePlayer& player) {
        devicePlayer = player;
        hasProfile = true;
    }

    /** Clears the session profile. */
    void clear() {
        devicePlayer = DevicePlayer();
        hasProfile = false;
    }

private:
    DevicePlayer devicePlayer;
    bool hasProfile = false;
};
