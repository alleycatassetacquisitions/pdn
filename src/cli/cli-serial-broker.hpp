#pragma once

#ifdef NATIVE_BUILD

#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "device/drivers/native/native-serial-driver.hpp"

namespace cli {

/**
 * Identifies which physical jack on a device.
 */
enum class JackType {
    OUTPUT_JACK,  // The "output" jack (serial_out)
    INPUT_JACK    // The "input" jack (serial_in)
};

/**
 * Connection between two devices via serial cable.
 * 
 * Connection rules based on device roles (from DeviceSerial):
 * - Hunters use OUTPUT_JACK as their PRIMARY comms jack
 * - Bounties use INPUT_JACK as their PRIMARY comms jack
 * 
 * For Hunter ↔ Bounty (different roles): PRIMARY to PRIMARY means:
 *   → Hunter.outputJack ↔ Bounty.inputJack
 * 
 * For same roles (Hunter ↔ Hunter or Bounty ↔ Bounty): PRIMARY to AUXILIARY
 *   → Hunter A.outputJack ↔ Hunter B.inputJack
 *   → Bounty A.inputJack ↔ Bounty B.outputJack
 */
struct CableConnection {
    int deviceA;
    int deviceB;
    JackType jackA;  // Which jack on device A
    JackType jackB;  // Which jack on device B
    bool sameRole;   // True if Hunter-Hunter or Bounty-Bounty
};

/**
 * Broker that manages serial cable connections between simulated devices.
 * Routes data between the appropriate jacks based on device roles.
 */
class SerialCableBroker {
public:
    static SerialCableBroker& getInstance() {
        static SerialCableBroker instance;
        return instance;
    }
    
    // Prevent copying
    SerialCableBroker(const SerialCableBroker&) = delete;
    SerialCableBroker& operator=(const SerialCableBroker&) = delete;
    
    /**
     * Register a device's serial drivers with the broker.
     * @param deviceIndex Unique index for this device
     * @param outputJack The device's output jack driver
     * @param inputJack The device's input jack driver
     * @param isHunter Whether this device is a hunter (affects connection routing)
     */
    void registerDevice(int deviceIndex, 
                        NativeSerialDriver* outputJack, 
                        NativeSerialDriver* inputJack,
                        bool isHunter) {
        DeviceSerial ds;
        ds.outputJack = outputJack;
        ds.inputJack = inputJack;
        ds.isHunter = isHunter;
        devices[deviceIndex] = ds;
    }
    
    /**
     * Unregister a device from the broker.
     */
    void unregisterDevice(int deviceIndex) {
        // Remove any connections involving this device
        disconnectDevice(deviceIndex);
        devices.erase(deviceIndex);
    }

    /**
     * Connect two devices with a serial cable.
     * Automatically determines jack routing based on device roles:
     * - Different roles (Hunter ↔ Bounty): Hunter.output ↔ Bounty.input (PRIMARY to PRIMARY)
     * - Same roles: output ↔ input (PRIMARY to AUXILIARY)
     *
     * @return true if connection was made, false if invalid devices
     */
    bool connect(int deviceA, int deviceB) {
        auto itA = devices.find(deviceA);
        auto itB = devices.find(deviceB);

        if (itA == devices.end() || itB == devices.end()) {
            return false;
        }

        // Check if already connected
        for (const auto& conn : connections) {
            if ((conn.deviceA == deviceA && conn.deviceB == deviceB) ||
                (conn.deviceA == deviceB && conn.deviceB == deviceA)) {
                return true;  // Already connected
            }
        }

        bool sameRole = (itA->second.isHunter == itB->second.isHunter);
        bool aIsHunter = itA->second.isHunter;
        bool bIsHunter = itB->second.isHunter;
        
        CableConnection conn;
        conn.deviceA = deviceA;
        conn.deviceB = deviceB;
        conn.sameRole = sameRole;
        
        // Determine which jack each device uses as PRIMARY:
        // - Hunters use OUTPUT_JACK as PRIMARY
        // - Bounties use INPUT_JACK as PRIMARY
        
        if (sameRole) {
            // Same role: PRIMARY to AUXILIARY
            // Device A uses its PRIMARY, Device B uses its AUXILIARY (opposite of PRIMARY)
            if (aIsHunter) {
                // Hunter-Hunter: A.output(primary) ↔ B.input(auxiliary)
                conn.jackA = JackType::OUTPUT_JACK;
                conn.jackB = JackType::INPUT_JACK;
            } else {
                // Bounty-Bounty: A.input(primary) ↔ B.output(auxiliary)
                conn.jackA = JackType::INPUT_JACK;
                conn.jackB = JackType::OUTPUT_JACK;
            }
        } else {
            // Different roles: PRIMARY to PRIMARY
            // Each device uses its PRIMARY jack
            // Hunter's PRIMARY = OUTPUT_JACK, Bounty's PRIMARY = INPUT_JACK
            conn.jackA = aIsHunter ? JackType::OUTPUT_JACK : JackType::INPUT_JACK;
            conn.jackB = bIsHunter ? JackType::OUTPUT_JACK : JackType::INPUT_JACK;
        }

        NativeSerialDriver* jackA = getJack(itA->second, conn.jackA);
        NativeSerialDriver* jackB = getJack(itB->second, conn.jackB);
        if (jackA != nullptr && jackB != nullptr) {
            jackA->routeTo(jackB);
            jackB->routeTo(jackA);
        }

        connections.push_back(conn);
        return true;
    }
    
    /**
     * Disconnect two devices.
     * @return true if disconnected, false if not connected
     */
    bool disconnect(int deviceA, int deviceB) {
        for (std::vector<CableConnection>::iterator it = connections.begin();
             it != connections.end(); ++it) {
            if ((it->deviceA == deviceA && it->deviceB == deviceB) ||
                (it->deviceA == deviceB && it->deviceB == deviceA)) {
                unrouteConnection(*it);
                connections.erase(it);
                return true;
            }
        }
        return false;
    }
    
    /**
     * Disconnect all cables from a device.
     */
    void disconnectDevice(int deviceIndex) {
        for (const CableConnection& conn : connections) {
            if (conn.deviceA == deviceIndex || conn.deviceB == deviceIndex) {
                unrouteConnection(conn);
            }
        }
        connections.erase(std::remove_if(connections.begin(), connections.end(),
                                         [deviceIndex](const CableConnection& c) {
                                             return c.deviceA == deviceIndex ||
                                                    c.deviceB == deviceIndex;
                                         }),
                          connections.end());
    }

    /**
     * Check if a device is connected to another.
     * @return The index of the connected device, or -1 if not connected
     */
    int getConnectedDevice(int deviceIndex) const {
        for (const auto& conn : connections) {
            if (conn.deviceA == deviceIndex) return conn.deviceB;
            if (conn.deviceB == deviceIndex) return conn.deviceA;
        }
        return -1;
    }
    
    /**
     * Get all current connections.
     */
    const std::vector<CableConnection>& getConnections() const { return connections; }

    /**
     * Get connection count.
     */
    size_t getConnectionCount() const { return connections.size(); }

    /**
     * Get a string describing a connection for display.
     */
    std::string getConnectionDescription(const CableConnection& conn) const {
        std::string jackAStr = (conn.jackA == JackType::OUTPUT_JACK) ? "out" : "in";
        std::string jackBStr = (conn.jackB == JackType::OUTPUT_JACK) ? "out" : "in";
        std::string typeStr = conn.sameRole ? "P-to-A" : "P-to-P";
        return std::to_string(conn.deviceA) + "." + jackAStr + 
               " <-> " + 
               std::to_string(conn.deviceB) + "." + jackBStr +
               " (" + typeStr + ")";
    }

private:
    SerialCableBroker() = default;
    
    struct DeviceSerial {
        NativeSerialDriver* outputJack;
        NativeSerialDriver* inputJack;
        bool isHunter;
    };

    std::map<int, DeviceSerial> devices;
    std::vector<CableConnection> connections;

    NativeSerialDriver* getJack(const DeviceSerial& device, JackType type) {
        return (type == JackType::OUTPUT_JACK) ? device.outputJack : device.inputJack;
    }

    void unrouteConnection(const CableConnection& conn) {
        std::map<int, DeviceSerial>::iterator itA = devices.find(conn.deviceA);
        std::map<int, DeviceSerial>::iterator itB = devices.find(conn.deviceB);
        if (itA != devices.end()) {
            NativeSerialDriver* jackA = getJack(itA->second, conn.jackA);
            if (jackA != nullptr) jackA->routeTo(nullptr);
        }
        if (itB != devices.end()) {
            NativeSerialDriver* jackB = getJack(itB->second, conn.jackB);
            if (jackB != nullptr) jackB->routeTo(nullptr);
        }
    }
};

} // namespace cli

#endif // NATIVE_BUILD
