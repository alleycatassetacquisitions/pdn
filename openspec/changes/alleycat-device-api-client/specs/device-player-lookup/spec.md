## ADDED Requirements

### Requirement: Lookup uses the protobuf device endpoint
The client SHALL queue a `GET` request to `/device/players/by-pin/{pin}` with no request body. The PIN is not validated on the client; the server rejects malformed PINs.

#### Scenario: Request shape
- **WHEN** a lookup is requested for PIN `"0007"`
- **THEN** the queued request has method `GET`, path `/device/players/by-pin/0007`, and an empty payload

### Requirement: Successful response is decoded into a device player
On a successful response, the client SHALL decode the body as `DevicePlayerResponse` and pass a `DevicePlayer` to the success callback. The `DevicePlayer` SHALL contain the player UUID, name, team (if present), and affiliations in server order. UUIDs arrive as 16 raw bytes and SHALL be held as 16 bytes; an absent team SHALL be held as the nil UUID. Display names arrive as unterminated ASCII of up to 255 bytes and SHALL be held as terminated strings of at most 36 characters, truncating longer names. An absent name SHALL be held as the empty string. The device SHALL keep at most three affiliations.

#### Scenario: Player with team and affiliations
- **WHEN** the response contains a team and two affiliations
- **THEN** the `DevicePlayer` reports that it has a team, with the team UUID and name
- **AND** its affiliations list has two entries in response order, each with category UUID, category name, affiliation UUID, and affiliation name

#### Scenario: Unassigned player
- **WHEN** the response omits `team_id` and `team_name` and has no affiliations
- **THEN** the `DevicePlayer` reports that it has no team
- **AND** its affiliations list is empty

#### Scenario: Maximum-length name
- **WHEN** the response contains a 36-byte ASCII player name
- **THEN** the decoded name equals the full 36 bytes

#### Scenario: Long display names
- **WHEN** the player, team, category, or affiliation name is longer than 36 bytes
- **THEN** the response is still decoded
- **AND** each decoded name holds the first 36 bytes

#### Scenario: Absent name
- **WHEN** the response omits the player name
- **THEN** the decoded name is the empty string

#### Scenario: Maximum affiliations
- **WHEN** the response contains three affiliations
- **THEN** the `DevicePlayer` has three affiliations

#### Scenario: Affiliations past the device limit
- **WHEN** the response contains more than three affiliations
- **THEN** the response is still decoded
- **AND** the `DevicePlayer` holds the first three affiliations in response order

### Requirement: Undecodable responses are reported as errors
If a successful HTTP response body cannot be decoded as `DevicePlayerResponse`, the client SHALL invoke the error callback with `WirelessError::INVALID_RESPONSE` and SHALL NOT invoke the success callback.

#### Scenario: Garbage body
- **WHEN** the response body is bytes that are not a valid `DevicePlayerResponse`
- **THEN** the error callback receives `WirelessError::INVALID_RESPONSE`

#### Scenario: Malformed UUID
- **WHEN** the player, team, category, or affiliation ID is present but not exactly 16 bytes
- **THEN** the response is treated as undecodable

#### Scenario: Missing required UUID
- **WHEN** the player, category, or affiliation ID is absent
- **THEN** the response is treated as undecodable

### Requirement: Transport errors are forwarded unchanged
If the HTTP layer reports an error, including a non-2xx status such as 404 for an unknown PIN or 400 for a malformed one, the client SHALL pass the `WirelessErrorInfo` to the error callback without modification.

#### Scenario: Unknown PIN
- **WHEN** the HTTP layer reports an error for the lookup request
- **THEN** the error callback receives the same `WirelessErrorInfo`
