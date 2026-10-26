## ADDED Requirements

### Requirement: PlayerFetch is not mounted from Quickdraw
This change SHALL deliver `PlayerFetchApp` as a compilable standalone state machine. Quickdraw and the default device boot path SHALL continue to use `PlayerRegistrationApp` without referencing PlayerFetch.

#### Scenario: Quickdraw unchanged
- **WHEN** firmware boots into the normal Quickdraw state map
- **THEN** only the legacy registration app is used for pairing
- **AND** PlayerFetch is not instantiated by Quickdraw

### Requirement: Pin entry matches legacy pairing UX
Pin entry SHALL reproduce the digit-entry behavior of `player-registration-state.cpp` (display, buttons, four-digit completion, `setUserID`, `applyRngSeedFromUserId`). It SHALL NOT depend on `MatchManager` or show transmit-breath for stored matches.

#### Scenario: Non-demo PIN completed
- **WHEN** the player confirms digits `0`, `0`, `0`, `7`
- **THEN** `Player` holds PIN `"0007"` and RNG is seeded from that PIN
- **AND** the app transitions to fetch user

#### Scenario: Demo PIN completed
- **WHEN** the player confirms digits `0`, `0`, `0`, `0`
- **THEN** `Player` holds PIN `"0000"` and RNG is seeded
- **AND** the app transitions to choose team without issuing a network request

### Requirement: Fetch user only performs device player lookup
Fetch user SHALL call `PlayerApi::getDevicePlayerByPin` with the stored PIN, SHALL show loading glyphs while waiting, and SHALL use the same fetch timeout constant as legacy fetch user. It SHALL NOT upload matches, SHALL NOT call `PlayerApi::getPlayer`, and SHALL NOT branch on test or debug PIN constants.

#### Scenario: Lookup queued
- **WHEN** fetch user mounts with PIN `"1234"`
- **THEN** the wireless layer receives `GET /device/players/by-pin/1234` with an empty body

### Requirement: Successful fetch stores DevicePlayer and shows welcome
On success, fetch user SHALL store the decoded `DevicePlayer` in the PlayerFetch session, stop the fetch timer, and transition to welcome.

#### Scenario: Decodable response
- **WHEN** lookup succeeds with a valid `DevicePlayer`
- **THEN** the session exposes that profile for welcome display
- **AND** the app transitions to welcome

### Requirement: Fetch failure shows asset page zero then returns to pin entry
On lookup error or fetch timeout, fetch user SHALL display only the first confirm-offline screen (“Unable to” / “Locate Asset”), SHALL wait 3000 ms, and SHALL transition to pin entry.

#### Scenario: HTTP error
- **WHEN** the lookup error callback runs
- **THEN** page zero is shown
- **AND** after 3000 ms the app transitions to pin entry

#### Scenario: Timer expiry
- **WHEN** the fetch timer expires before success
- **THEN** page zero is shown
- **AND** after 3000 ms the app transitions to pin entry

### Requirement: Choose team demo enters stub only
Choose team SHALL offer Hunter/Bounty selection like legacy choose-role. On confirm, the app SHALL transition to a demo stub state. It SHALL NOT mount welcome or awaken on the demo path.

#### Scenario: Demo confirm
- **WHEN** the player confirms a team on the demo path
- **THEN** demo stub mounts
- **AND** welcome is not mounted

### Requirement: Demo stub must not crash
Demo stub SHALL be a placeholder implementation sufficient to exercise the transition from choose team. It SHALL NOT transition to Quickdraw Idle or gameplay states.

#### Scenario: Demo stub mounted
- **WHEN** demo stub is active
- **THEN** the app remains entirely within PlayerFetch
- **AND** no unhandled null profile access occurs

### Requirement: Welcome then awaken on server path only
Welcome SHALL display `Welcome` and the fetched player name, then after the welcome timer SHALL transition to awaken sequence within PlayerFetch.

#### Scenario: Server registration path
- **WHEN** welcome completes its timer
- **THEN** awaken sequence mounts inside PlayerFetch

### Requirement: No external gameplay handoff
PlayerFetch SHALL NOT add transitions from this app to Quickdraw Idle or other gameplay states in this change. `readyForGameplay()` MAY become true after awaken for future wiring but SHALL NOT be consumed by Quickdraw yet.

#### Scenario: After awaken on server path
- **WHEN** awaken sequence completes
- **THEN** Quickdraw does not transition to Idle as a result of PlayerFetch

### Requirement: Firmware OTA from pin entry preserved
Pin entry SHALL support secondary-button hold to firmware update with the same timing and transitions as legacy registration, hosted on PlayerFetch.

#### Scenario: OTA hold
- **WHEN** the secondary button is held for `FIRMWARE_UPDATE_HOLD_MS` on pin entry
- **THEN** the app transitions to registration firmware update and returns to pin entry when done

### Requirement: Legacy offline registration not used
PlayerFetch SHALL NOT include confirm-offline menu states, offline choose-role after fetch failure, or JSON player lookup.

#### Scenario: Failed fetch
- **WHEN** lookup fails
- **THEN** the app never offers offline confirm or manual role selection after fetch
