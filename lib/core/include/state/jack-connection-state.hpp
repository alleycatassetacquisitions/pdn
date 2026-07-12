#pragma once

#include "device/device-type.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

/// Peer facts the RDC delivered for one jack, forwarded opaquely: core never
/// decodes the profile bytes — the game layer does at the delivery site.
struct ConnectionContext {
    DeviceType peerType = DeviceType::UNKNOWN;
    uint8_t chainRole = 0;
    /// PlayerProfile is the larger of the two profile kinds, so one fixed
    /// buffer holds either; profileLen is the received byte count.
    static_assert(sizeof(FdnProfile) <= sizeof(PlayerProfile),
                  "profile buffer is sized by the larger profile (PlayerProfile); "
                  "FdnProfile growth past it must bump the buffer type");
    std::array<uint8_t, sizeof(PlayerProfile)> profile{};
    size_t profileLen = 0;
};

/// One jack's connection snapshot as delivered to the current state.
struct JackConnectionState {
    bool connected = false;
    /// Present only when the peer's context arrived for this connection;
    /// nullopt on disconnect and on a connect whose context never arrived.
    std::optional<ConnectionContext> context;
};
