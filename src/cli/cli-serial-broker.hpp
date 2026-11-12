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
 * Keeps the list of serial cables between simulated devices and picks, from the
 * device roles, which jacks each cable joins. The drivers carry the bytes: the
 * broker only tells each jack where its TX goes.
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
     * Unregister a device from the broker. Has to run before the device's serial
     * drivers are destroyed: the far end of each of its cables points at them.
     */
    void unregisterDevice(int deviceIndex) {
        // Remove any connections involving this device
        disconnectDevice(deviceIndex);
        devices.erase(deviceIndex);
    }

    /**
     * Connect two devices with a serial cable.
     * Automatically determines jack routing based on device roles:
     * - Different roles (Hunter ↔ Bounty): hunter output ↔ bounty input (PRIMARY to PRIMARY)
     * - Same roles: output ↔ input (PRIMARY to AUXILIARY)
     *
     * @return true if the two are now cabled; false for an unknown device or
     *         when either jack the cable would use already has one
     */
    bool connect(int deviceA, int deviceB) {
        std::map<int, DeviceSerial>::iterator itA = devices.find(deviceA);
        std::map<int, DeviceSerial>::iterator itB = devices.find(deviceB);

        if (itA == devices.end() || itB == devices.end()) {
            return false;
        }

        // Check if already connected
        for (const CableConnection& existing : connections) {
            if ((existing.deviceA == deviceA && existing.deviceB == deviceB) ||
                (existing.deviceA == deviceB && existing.deviceB == deviceA)) {
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

        // One cable per jack: a second would have nowhere to send.
        if (isJackCabled(deviceA, conn.jackA) || isJackCabled(deviceB, conn.jackB)) {
            return false;
        }

        connections.push_back(conn);
        route(conn, true);
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
                route(*it, false);
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
            if (conn.deviceA == deviceIndex || conn.deviceB == deviceIndex) route(conn, false);
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
        for (const CableConnection& conn : connections) {
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

    bool isJackCabled(int deviceIndex, JackType jack) const {
        for (const CableConnection& conn : connections) {
            if (conn.deviceA == deviceIndex && conn.jackA == jack) return true;
            if (conn.deviceB == deviceIndex && conn.jackB == jack) return true;
        }
        return false;
    }

    // Joins the two jacks a cable names, or parts them.
    void route(const CableConnection& conn, bool joined) {
        std::map<int, DeviceSerial>::iterator itA = devices.find(conn.deviceA);
        std::map<int, DeviceSerial>::iterator itB = devices.find(conn.deviceB);
        if (itA == devices.end() || itB == devices.end()) return;

        NativeSerialDriver* jackA = getJack(itA->second, conn.jackA);
        NativeSerialDriver* jackB = getJack(itB->second, conn.jackB);
        jackA->routeTo(joined ? jackB : nullptr);
        jackB->routeTo(joined ? jackA : nullptr);
    }
};

} // namespace cli

#endif // NATIVE_BUILD
