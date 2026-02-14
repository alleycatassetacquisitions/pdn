# Network Discovery & Pairing System

## Overview

The network discovery system allows PDN devices to discover each other and establish pairing for multiplayer duels. It works on both ESP32 hardware (using ESP-NOW) and the CLI simulator (using the NativePeerBroker).

## Architecture

### Discovery Protocol

The system uses a simple binary protocol layered on top of the existing peer communication system:

- **Packet Type**: Uses `PktType::kPlayerInfoBroadcast` for all discovery messages
- **Packet Size**: 39 bytes (fits easily within ESP-NOW's 250-byte limit)
- **Transport**:
  - ESP32: ESP-NOW broadcast (FF:FF:FF:FF:FF:FF)
  - CLI: NativePeerBroker simulated broadcast

### Packet Format

```c++
struct DiscoveryPacket {
    uint8_t type;           // DiscoveryPacketType (0x01-0x04)
    uint8_t version;        // Protocol version (0x01)
    char deviceId[16];      // Device ID (null-padded)
    char playerName[16];    // Player name (null-padded)
    uint8_t allegiance;     // 0=NONE, 1=ENDLINE, 2=PRESTIGE, 3=RESISTANCE
    uint8_t level;          // Player level
    uint8_t available;      // 0=busy, 1=available
    uint8_t reserved[2];    // Reserved for future use
} __attribute__((packed));
```

### Packet Types

| Type | Value | Description |
|------|-------|-------------|
| `PRESENCE` | 0x01 | Broadcast presence announcement |
| `PAIR_REQUEST` | 0x02 | Request pairing with target device |
| `PAIR_ACCEPT` | 0x03 | Accept a pairing request |
| `PAIR_REJECT` | 0x04 | Reject a pairing request |

### State Machine

```
IDLE
  ↓ startScan()
SCANNING
  ↓ requestPair()
PAIR_REQUEST_SENT
  ↓ receive PAIR_ACCEPT
PAIRED
```

Or when receiving a request:

```
IDLE / SCANNING
  ↓ receive PAIR_REQUEST
PAIR_REQUEST_RECEIVED
  ↓ acceptPair()
PAIRED
```

## API Reference

### Initialization

```c++
DeviceDiscovery discovery;
discovery.init("HUNTER-001", "Alice");
```

### Scanning

```c++
// Start scanning for 5 seconds
discovery.startScan(5000);

// Check if scanning
if (discovery.isScanning()) {
    // ...
}

// Get discovered devices
std::vector<DeviceInfo> devices = discovery.getDiscoveredDevices();

// Stop scan early
discovery.stopScan();
```

### Pairing

```c++
// Request pairing
discovery.requestPair("BOUNTY-001");

// Accept incoming request
discovery.acceptPair("HUNTER-001");

// Reject incoming request
discovery.rejectPair("HUNTER-001");

// Unpair
discovery.unpair();

// Check pairing state
PairingState state = discovery.getPairingState();

// Get paired device
const DeviceInfo* paired = discovery.getPairedDevice();
if (paired) {
    std::cout << "Paired with: " << paired->deviceId << "\n";
}
```

### Callbacks

```c++
// Called when a new device is discovered
discovery.onDeviceFound([](const DeviceInfo& info) {
    std::cout << "Found: " << info.deviceId << " (" << info.playerName << ")\n";
});

// Called when a pair request is received
discovery.onPairRequest([](const DeviceInfo& info) {
    std::cout << "Pair request from: " << info.deviceId << "\n";
});

// Called when pairing completes (success or failure)
discovery.onPairComplete([](bool success) {
    std::cout << "Pairing " << (success ? "succeeded" : "failed") << "\n";
});
```

### Update Loop

```c++
// Call every game tick
uint32_t currentTimeMs = millis();  // or clock.getMillis()
discovery.update(currentTimeMs);
```

## CLI Integration

### Proposed CLI Commands

#### `scan [duration]`

Start scanning for nearby devices.

```bash
> scan
Scanning for nearby devices... (5s)

Found 2 devices:
  [0] HUNTER-002 - "Bob" — RESISTANCE — Lv.5 — Available — Signal: -42dBm
  [1] BOUNTY-003 - "Charlie" — ENDLINE — Lv.3 — In Game — Signal: -67dBm

Use: pair <device_id> to send pairing request
```

```bash
> scan 10
Scanning for 10 seconds...
```

#### `scan stop`

Stop scanning early.

```bash
> scan stop
Scan stopped. Found 1 device.
```

#### `pair <device_id>`

Send a pairing request.

```bash
> pair HUNTER-002
Sending pair request to "Bob"...
Waiting for response...

✓ Paired with "Bob" (RESISTANCE, Lv.5)
Ready for duel!
```

#### `pair accept`

Accept an incoming pairing request.

```bash
Pair request from "Bob" (ENDLINE, Lv.3)

> pair accept
✓ Paired with "Bob"
```

#### `pair reject`

Reject an incoming pairing request.

```bash
> pair reject
Pair request rejected
```

#### `unpair`

Disconnect from paired device.

```bash
> unpair
Unpaired from "Bob"
```

#### `pair status`

Show current pairing state.

```bash
> pair status
State: PAIRED
Paired with: HUNTER-002 ("Bob")
```

## Integration Steps

### 1. Add Discovery to DeviceInstance

In `cli-device.hpp`, add discovery to the device structure:

```c++
#include "network/discovery.hpp"

struct DeviceInstance {
    // ... existing fields ...

    DeviceDiscovery discovery;

    // In constructor/initialization:
    discovery.init(deviceId, "Player " + deviceId);
};
```

### 2. Register Packet Handler

In device initialization code:

```c++
// Register discovery packet handler
peerCommsDriver->setPacketHandler(
    PktType::kPlayerInfoBroadcast,
    [](const uint8_t* src, const uint8_t* data, size_t len, void* ctx) {
        DeviceDiscovery* discovery = static_cast<DeviceDiscovery*>(ctx);
        discovery->handleDiscoveryPacket(src, data, len);
    },
    &deviceInstance.discovery
);
```

### 3. Add CLI Commands

In `cli-commands.hpp`, add to `CommandProcessor::execute()`:

```c++
if (command == "scan") {
    return cmdScan(tokens, devices, selectedDevice);
}
if (command == "pair") {
    return cmdPair(tokens, devices, selectedDevice);
}
if (command == "unpair") {
    return cmdUnpair(tokens, devices, selectedDevice);
}
```

### 4. Connect to Peer Communications

Modify `DeviceDiscovery` constructor to accept a `PeerCommsDriverInterface*`:

```c++
// In discovery.hpp
class DeviceDiscovery {
public:
    DeviceDiscovery(PeerCommsDriverInterface* peerComms);
    // ...
private:
    PeerCommsDriverInterface* peerComms_;
};

// In sendDiscoveryPacket()
void DeviceDiscovery::sendDiscoveryPacket(const uint8_t* dstMac, DiscoveryPacketType type) {
    DiscoveryPacket packet = buildPacket(type);
    peerComms_->sendData(dstMac, PktType::kPlayerInfoBroadcast,
                         reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
}
```

## Timeouts and Error Handling

| Timeout | Duration | Behavior |
|---------|----------|----------|
| Scan timeout | 5s (default) | Automatically stops scanning |
| Device staleness | 10s | Removes devices not seen in 10 seconds |
| Pair request timeout | 15s | Cancels pending pair request |

## Testing

### Unit Tests

```bash
# Run discovery demo/test
g++ test/test_network_discovery.cpp src/network/discovery.cpp -Iinclude -o test_discovery
./test_discovery
```

### CLI Simulator Testing

```bash
# Start CLI with 2 devices
pdncli 2

# On device 0
> select 0
> scan
> pair BOUNTY-001

# On device 1
> select 1
> pair accept
```

## Future Enhancements

1. **RSSI Tracking**: Implement actual signal strength measurement
2. **Auto-pairing**: Automatically accept pairs from known devices
3. **Persistent Pairings**: Save paired devices across reboots
4. **Multi-device Discovery**: Support discovering multiple devices simultaneously
5. **Encryption**: Add optional encryption for discovery packets
6. **mDNS Integration**: Use mDNS on CLI for true local network discovery
7. **UDP Broadcast**: Implement UDP broadcast on CLI for cross-machine discovery

## Security Considerations

- Discovery packets are sent in plaintext
- No authentication of pairing requests
- Broadcast nature means any nearby device can see presence announcements
- For production use, consider adding:
  - Cryptographic signatures on pairing requests
  - Challenge-response authentication
  - Encrypted device info fields

## References

- ESP-NOW Protocol: [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- PDN Peer Communication: `include/device/drivers/peer-comms-interface.hpp`
- CLI Simulator: `include/cli/cli-device.hpp`
