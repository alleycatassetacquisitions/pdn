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
 * - FDN devices use OUTPUT_JACK as their PRIMARY comms jack (same as hunters)
 *
 * For Hunter ↔ Bounty (different roles): PRIMARY to PRIMARY means:
 *   → Hunter.outputJack ↔ Bounty.inputJack
 *
 * For same roles (Hunter ↔ Hunter or Bounty ↔ Bounty): PRIMARY to PRIMARY
 *   → Hunter A.outputJack ↔ Hunter B.outputJack (both use active comms jack)
 *   → Bounty A.inputJack ↔ Bounty B.inputJack (both use active comms jack)
 *   → FDN.outputJack ↔ Hunter.outputJack (both use active comms jack)
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
        devices_[deviceIndex] = ds;
    }
    
    /**
     * Unregister a device from the broker.
     */
    void unregisterDevice(int deviceIndex) {
        // Remove any connections involving this device
        disconnectDevice(deviceIndex);
        devices_.erase(deviceIndex);
    }
    
    /**
     * Connect two devices with a serial cable.
     * Automatically determines jack routing based on device roles:
     * - Different roles (Hunter ↔ Bounty): PRIMARY to PRIMARY (output ↔ input)
     * - Same roles: PRIMARY to PRIMARY (output ↔ output OR input ↔ input)
     *
     * @return true if connection was made, false if invalid devices
     */
    bool connect(int deviceA, int deviceB) {
        auto itA = devices_.find(deviceA);
        auto itB = devices_.find(deviceB);
        
        if (itA == devices_.end() || itB == devices_.end()) {
            return false;
        }
        
        // Check if already connected
        for (const auto& conn : connections_) {
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
            // Same role: PRIMARY to PRIMARY (both devices use their active comms jack)
            // This ensures FDN (uses OUTPUT_JACK) can communicate with Player devices of same role
            if (aIsHunter) {
                // Hunter-Hunter or FDN-Hunter: A.output(primary) ↔ B.output(primary)
                conn.jackA = JackType::OUTPUT_JACK;
                conn.jackB = JackType::OUTPUT_JACK;
            } else {
                // Bounty-Bounty: A.input(primary) ↔ B.input(primary)
                conn.jackA = JackType::INPUT_JACK;
                conn.jackB = JackType::INPUT_JACK;
            }
        } else {
            // Different roles: PRIMARY to PRIMARY
            // Each device uses its PRIMARY jack
            // Hunter's PRIMARY = OUTPUT_JACK, Bounty's PRIMARY = INPUT_JACK
            conn.jackA = aIsHunter ? JackType::OUTPUT_JACK : JackType::INPUT_JACK;
            conn.jackB = bIsHunter ? JackType::OUTPUT_JACK : JackType::INPUT_JACK;
        }
        
        connections_.push_back(conn);

        // Clear accumulated output buffers to prevent message flood
        // when connecting to a device that was broadcasting before connection
        NativeSerialDriver* jackA = getJack(itA->second, conn.jackA);
        NativeSerialDriver* jackB = getJack(itB->second, conn.jackB);
        jackA->clearOutput();
        jackB->clearOutput();

        return true;
    }
    
    /**
     * Disconnect two devices.
     * @return true if disconnected, false if not connected
     */
    bool disconnect(int deviceA, int deviceB) {
        for (auto it = connections_.begin(); it != connections_.end(); ++it) {
            if ((it->deviceA == deviceA && it->deviceB == deviceB) ||
                (it->deviceA == deviceB && it->deviceB == deviceA)) {
                connections_.erase(it);
                return true;
            }
        }
        return false;
    }
    
    /**
     * Disconnect all cables from a device.
     */
    void disconnectDevice(int deviceIndex) {
        connections_.erase(
            std::remove_if(connections_.begin(), connections_.end(),
                [deviceIndex](const CableConnection& c) {
                    return c.deviceA == deviceIndex || c.deviceB == deviceIndex;
                }),
            connections_.end());
    }
    
    /**
     * Transfer pending serial data between connected devices.
     * Should be called from the main loop.
     */
    void transferData() {
        for (const auto& conn : connections_) {
            auto itA = devices_.find(conn.deviceA);
            auto itB = devices_.find(conn.deviceB);
            
            if (itA == devices_.end() || itB == devices_.end()) continue;
            
            // Get the appropriate jacks based on connection type
            NativeSerialDriver* jackA = getJack(itA->second, conn.jackA);
            NativeSerialDriver* jackB = getJack(itB->second, conn.jackB);
            
            // Transfer A → B: A's jack output goes to B's jack input
            transferFromTo(jackA, jackB);
            
            // Transfer B → A: B's jack output goes to A's jack input
            transferFromTo(jackB, jackA);
        }
    }
    
    /**
     * Check if a device is connected to another.
     * @return The index of the connected device, or -1 if not connected
     */
    int getConnectedDevice(int deviceIndex) const {
        for (const auto& conn : connections_) {
            if (conn.deviceA == deviceIndex) return conn.deviceB;
            if (conn.deviceB == deviceIndex) return conn.deviceA;
        }
        return -1;
    }
    
    /**
     * Get all current connections.
     */
    const std::vector<CableConnection>& getConnections() const {
        return connections_;
    }
    
    /**
     * Get connection count.
     */
    size_t getConnectionCount() const {
        return connections_.size();
    }

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

    /**
     * Reset the singleton instance to a clean state.
     * Must be called between tests to prevent state pollution.
     */
    static void resetInstance() {
        auto& instance = getInstance();
        instance.devices_.clear();
        instance.connections_.clear();
    }

private:
    SerialCableBroker() = default;
    
    struct DeviceSerial {
        NativeSerialDriver* outputJack;
        NativeSerialDriver* inputJack;
        bool isHunter;
    };
    
    std::map<int, DeviceSerial> devices_;
    std::vector<CableConnection> connections_;
    
    NativeSerialDriver* getJack(const DeviceSerial& device, JackType type) {
        return (type == JackType::OUTPUT_JACK) ? device.outputJack : device.inputJack;
    }
    
    /**
     * Transfer data from one jack's output buffer to another jack's input.
     */
    void transferFromTo(NativeSerialDriver* from, NativeSerialDriver* to) {
        std::string data = from->getOutput();
        if (!data.empty()) {
            // Parse into messages (split by newlines, which is how println works)
            size_t pos = 0;
            while ((pos = data.find('\n')) != std::string::npos) {
                std::string msg = data.substr(0, pos);
                if (!msg.empty()) {
                    // The message already has STRING_START (*) prepended by writeString
                    // Just add the terminator that DeviceSerial expects
                    to->injectInput(msg + "\r");
                }
                data.erase(0, pos + 1);
            }
            from->clearOutput();
        }
    }
};

} // namespace cli

#endif // NATIVE_BUILD
