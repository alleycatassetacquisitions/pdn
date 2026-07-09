## ADDED Requirements

### Requirement: fdnId is derived from the last 3 octets of the FDN MAC address
Each FDN SHALL compute its `fdnId` at boot from the last 3 octets of its WiFi MAC address, formatted as a 6-character lowercase hex string (e.g. `a1b2c3`). This value SHALL be used in all FDN-specific topic strings.

#### Scenario: FDN computes its fdnId
- **WHEN** the FDN boots and initialises MqttManager
- **THEN** it derives `fdnId` from its MAC and uses it for all topic subscriptions and publishes

### Requirement: FDN subscribes to its specific topic subtree and the broadcast topic
Each FDN SHALL subscribe to `pdn/fdn/{fdnId}/#` (all messages for this FDN) and `pdn/broadcast/#` (venue-wide messages for all FDNs).

#### Scenario: FDN receives its own config message
- **WHEN** MC publishes to `pdn/fdn/a1b2c3/config`
- **THEN** only the FDN with `fdnId = a1b2c3` receives and processes it

#### Scenario: FDN receives a broadcast event
- **WHEN** MC publishes to `pdn/broadcast/event`
- **THEN** all online FDNs receive and relay it to nearby PDNs

### Requirement: Downstream topic schema covers FDN config, app launch, and PDN relay
The following topics SHALL be defined as constants in `lib/core`:

| Topic | Direction | Purpose |
|---|---|---|
| `pdn/fdn/{fdnId}/config` | MC → FDN | FDN-level configuration (activate, deactivate, settings) |
| `pdn/fdn/{fdnId}/app/launch` | MC → FDN | Direct a specific FDN to launch an app or experience |
| `pdn/fdn/{fdnId}/pdns/render` | MC → FDN | Push render context (GamepadConfig + optional GameEvent) to PDNs connected to this FDN |
| `pdn/broadcast/event` | MC → all FDNs | Venue-wide event relayed to all nearby PDNs |

#### Scenario: MC activates a specific FDN
- **WHEN** MC publishes an activation payload to `pdn/fdn/a1b2c3/config`
- **THEN** only that FDN processes the activation; other FDNs are unaffected

### Requirement: Upstream topic schema covers FDN status and PDN event relay
The following topics SHALL be defined as constants in `lib/core`:

| Topic | Direction | Purpose |
|---|---|---|
| `pdn/fdn/{fdnId}/status` | FDN → MC | FDN online/offline heartbeat and connected player count |
| `pdn/fdn/{fdnId}/relay` | FDN → MC | PDN-originated events (match results, player registration requests) |

#### Scenario: FDN publishes a match result on behalf of a PDN
- **WHEN** a PDN sends a match result to the FDN via ESP-NOW
- **THEN** MqttManager publishes it to `pdn/fdn/{fdnId}/relay` with QoS 1 so HA can relay it to the Rust server
