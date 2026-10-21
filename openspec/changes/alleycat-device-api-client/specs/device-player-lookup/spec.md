## ADDED Requirements

### Requirement: Lookup uses the protobuf device endpoint
The client SHALL queue a `GET` request to `/device/players/by-pin/{pin}` with no request body. The PIN is not validated on the client; the server rejects malformed PINs.

#### Scenario: Request shape
- **WHEN** a lookup is requested for PIN `"0007"`
- **THEN** the queued request has method `GET`, path `/device/players/by-pin/0007`, and an empty payload

### Requirement: Successful response is decoded into a device player
On a successful response, the client SHALL decode the body as `DevicePlayerResponse` and pass a `DevicePlayer` to the success callback. The `DevicePlayer` SHALL contain the player UUID, event UUID, PIN, name, registration time, active status, team (if present), and affiliations in server order. Every UUID SHALL be held as 16 bytes, and an absent team SHALL be held as the nil UUID.

#### Scenario: Player with team and affiliations
- **WHEN** the response contains a team and two affiliations
- **THEN** the `DevicePlayer` reports that it has a team, with the team UUID and name
- **AND** its affiliations list has two entries in response order, each with category UUID, category name, affiliation UUID, and affiliation name

#### Scenario: Unassigned player
- **WHEN** the response omits `team_id` and `team_name` and has no affiliations
- **THEN** the `DevicePlayer` reports that it has no team
- **AND** its affiliations list is empty

#### Scenario: Inactive player
- **WHEN** the response has `active` set to false
- **THEN** the success callback is still invoked, with a `DevicePlayer` whose active status is false

#### Scenario: Maximum-length name
- **WHEN** the response contains a 36-byte ASCII player name
- **THEN** the decoded name equals the full 36 bytes

#### Scenario: Maximum affiliations
- **WHEN** the response contains three affiliations
- **THEN** the `DevicePlayer` has three affiliations

### Requirement: Undecodable responses are reported as errors
If a successful HTTP response body cannot be decoded as `DevicePlayerResponse`, the client SHALL invoke the error callback with `WirelessError::INVALID_RESPONSE` and SHALL NOT invoke the success callback.

#### Scenario: Garbage body
- **WHEN** the response body is bytes that are not a valid `DevicePlayerResponse`
- **THEN** the error callback receives `WirelessError::INVALID_RESPONSE`

#### Scenario: Value exceeds a device limit
- **WHEN** the response contains a string longer than 36 bytes or more than three affiliations
- **THEN** the response is treated as undecodable

#### Scenario: Malformed UUID
- **WHEN** the player, event, team, category, or affiliation ID is not 8-4-4-4-12 hexadecimal text
- **THEN** the response is treated as undecodable

### Requirement: Transport errors are forwarded unchanged
If the HTTP layer reports an error, including a non-2xx status such as 404 for an unknown PIN or 400 for a malformed one, the client SHALL pass the `WirelessErrorInfo` to the error callback without modification.

#### Scenario: Unknown PIN
- **WHEN** the HTTP layer reports an error for the lookup request
- **THEN** the error callback receives the same `WirelessErrorInfo`
