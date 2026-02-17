#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include "cli/cli-device-types.hpp"

namespace cli {

/**
 * Factory class for creating and managing DeviceInstance objects.
 * Implementations in src/cli/cli-device-factory.cpp.
 */
class DeviceFactory {
public:
    /**
     * Create a new simulated PDN device.
     */
    static DeviceInstance createDevice(int deviceIndex, bool isHunter);

    /**
     * Clean up a device instance and free all resources.
     */
    static void destroyDevice(DeviceInstance& device);

    /**
     * Create a new simulated FDN (Fixed Data Node) NPC device.
     */
    static DeviceInstance createFdnDevice(int deviceIndex, GameType gameType);

    /**
     * Create a standalone game device.
     */
    static DeviceInstance createGameDevice(int deviceIndex, const std::string& gameName);
};

} // namespace cli

#endif // NATIVE_BUILD
