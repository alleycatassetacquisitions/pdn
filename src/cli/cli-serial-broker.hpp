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
 * Connection from a PDN's output jack to one of the FDN's input jacks.
 */
struct FDNConnection {
    int pdnIndex;
    int fdnIndex;
    bool useSecondaryJack;  // false = INPUT_JACK, true = INPUT_JACK_SECONDARY
};

/**
 * Broker that manages serial cable connections between simulated devices.
 * Routes data between the appropriate jacks based on device roles.
 * Also supports FDN devices which have two input jacks and no output jack.
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
     * Register a PDN device's serial drivers with the broker.
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
     * Register an FDN device's serial drivers with the broker.
     * FDN has two input jacks and no output jack.
     * @param fdnIndex Unique index for this FDN (use a distinct namespace from PDN indices)
     * @param inputJack1 Primary input jack (INPUT_JACK)
     * @param inputJack2 Secondary input jack (INPUT_JACK_SECONDARY)
     */
    void registerFDN(int fdnIndex,
                     NativeSerialDriver* inputJack1,
                     NativeSerialDriver* inputJack2) {
        FDNSerial fs;
        fs.inputJack1 = inputJack1;
        fs.inputJack2 = inputJack2;
        fdnDevices_[fdnIndex] = fs;
    }

    /**
     * Unregister an FDN device from the broker.
     */
    void unregisterFDN(int fdnIndex) {
        disconnectAllFDN(fdnIndex);
        fdnDevices_.erase(fdnIndex);
    }

    /**
     * Connect a PDN's output jack to an FDN input jack.
     * Routes: PDN OUTPUT_JACK → FDN INPUT_JACK (jack 1) or INPUT_JACK_SECONDARY (jack 2)
     * @param pdnIndex PDN device index
     * @param fdnIndex FDN device index
     * @param useSecondaryJack true to use FDN INPUT_JACK_SECONDARY, false for INPUT_JACK
     * @return true if connection was made
     */
    bool connectPdnToFdn(int pdnIndex, int fdnIndex, bool useSecondaryJack = false) {
        if (devices_.find(pdnIndex) == devices_.end()) return false;
        if (fdnDevices_.find(fdnIndex) == fdnDevices_.end()) return false;

        // Check if already connected
        for (const auto& conn : fdnConnections_) {
            if (conn.pdnIndex == pdnIndex && conn.fdnIndex == fdnIndex) return true;
        }

        FDNConnection conn;
        conn.pdnIndex = pdnIndex;
        conn.fdnIndex = fdnIndex;
        conn.useSecondaryJack = useSecondaryJack;
        fdnConnections_.push_back(conn);
        return true;
    }

    /**
     * Disconnect a PDN from an FDN.
     * @return true if disconnected
     */
    bool disconnectPdnFromFdn(int pdnIndex, int fdnIndex) {
        for (auto it = fdnConnections_.begin(); it != fdnConnections_.end(); ++it) {
            if (it->pdnIndex == pdnIndex && it->fdnIndex == fdnIndex) {
                fdnConnections_.erase(it);
                return true;
            }
        }
        return false;
    }

    /**
     * Returns the FDN index a PDN is connected to, or -1 if not connected to any FDN.
     */
    int getConnectedFDN(int pdnIndex) const {
        for (const auto& conn : fdnConnections_) {
            if (conn.pdnIndex == pdnIndex) return conn.fdnIndex;
        }
        return -1;
    }

    /**
     * Returns the jack (false=primary, true=secondary) a PDN uses on the FDN, or false if not connected.
     */
    bool getConnectedFDNJack(int pdnIndex) const {
        for (const auto& conn : fdnConnections_) {
            if (conn.pdnIndex == pdnIndex) return conn.useSecondaryJack;
        }
        return false;
    }

    /**
     * Get all PDN→FDN connections.
     */
    const std::vector<FDNConnection>& getFDNConnections() const {
        return fdnConnections_;
    }

    /**
     * Unregister a PDN device from the broker.
     */
    void unregisterDevice(int deviceIndex) {
        // Remove any connections involving this device
        disconnectDevice(deviceIndex);
        devices_.erase(deviceIndex);
    }
    
    /**
     * Connect two devices with a serial cable.
     * Automatically determines jack routing based on device roles:
     * - Different roles (Hunter ↔ Bounty): output ↔ output (PRIMARY to PRIMARY)
     * - Same roles: output ↔ input (PRIMARY to AUXILIARY)
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
        
        connections_.push_back(conn);
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
     * Handles both PDN↔PDN and PDN→FDN connections.
     */
    void transferData() {
        // PDN↔PDN connections (bidirectional)
        for (const auto& conn : connections_) {
            auto itA = devices_.find(conn.deviceA);
            auto itB = devices_.find(conn.deviceB);
            
            if (itA == devices_.end() || itB == devices_.end()) continue;
            
            NativeSerialDriver* jackA = getJack(itA->second, conn.jackA);
            NativeSerialDriver* jackB = getJack(itB->second, conn.jackB);
            
            transferFromTo(jackA, jackB);
            transferFromTo(jackB, jackA);
        }

        // PDN→FDN connections (PDN output → FDN input only; FDN has no output)
        for (const auto& conn : fdnConnections_) {
            auto pdnIt  = devices_.find(conn.pdnIndex);
            auto fdnIt  = fdnDevices_.find(conn.fdnIndex);
            if (pdnIt == devices_.end() || fdnIt == fdnDevices_.end()) continue;

            NativeSerialDriver* pdnOut = pdnIt->second.outputJack;
            NativeSerialDriver* fdnIn  = conn.useSecondaryJack
                ? fdnIt->second.inputJack2
                : fdnIt->second.inputJack1;

            transferFromTo(pdnOut, fdnIn);
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

private:
    SerialCableBroker() = default;

    void disconnectAllFDN(int fdnIndex) {
        fdnConnections_.erase(
            std::remove_if(fdnConnections_.begin(), fdnConnections_.end(),
                [fdnIndex](const FDNConnection& c) { return c.fdnIndex == fdnIndex; }),
            fdnConnections_.end());
    }

    struct DeviceSerial {
        NativeSerialDriver* outputJack;
        NativeSerialDriver* inputJack;
        bool isHunter;
    };

    struct FDNSerial {
        NativeSerialDriver* inputJack1;   // INPUT_JACK (primary)
        NativeSerialDriver* inputJack2;   // INPUT_JACK_SECONDARY
    };

    std::map<int, DeviceSerial> devices_;
    std::vector<CableConnection> connections_;

    std::map<int, FDNSerial> fdnDevices_;
    std::vector<FDNConnection> fdnConnections_;
    
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
