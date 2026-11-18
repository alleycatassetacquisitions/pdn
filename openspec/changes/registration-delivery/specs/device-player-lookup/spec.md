## MODIFIED Requirements

### Requirement: Lookup uses the protobuf device endpoint
The client SHALL queue a `GET` request to `/device/players/by-band/{uid}` with no request body, where `{uid}` is the band UID as upper-case hexadecimal with no separators: `8`, `14` or `20` characters for a `4`-, `7`- or `10`-byte UID. A UID of any other length SHALL end the lookup as failed without queuing a request. The client SHALL NOT look a player up by PIN.

#### Scenario: Request shape
- **WHEN** a lookup is requested for the `4`-byte UID `04 a2 1b 9c`
- **THEN** the queued request has method `GET`, path `/device/players/by-band/04A21B9C`, and an empty payload

#### Scenario: Seven- and ten-byte UIDs
- **WHEN** lookups are requested for the `7`-byte UID `04 5E 21 7A 3B 6C 80` and the `10`-byte UID `00 01 23 45 67 89 AB CD EF FF`
- **THEN** the queued paths are `/device/players/by-band/045E217A3B6C80` and `/device/players/by-band/000123456789ABCDEFFF`

#### Scenario: UID of another length
- **WHEN** a lookup is requested for a UID of `0`, `3`, `5` or `9` bytes
- **THEN** no request is queued and the lookup ends as failed

### Requirement: Successful response is decoded into a player
On a successful response, the client SHALL decode the body as `DevicePlayerResponse` into a `Player` and report the lookup found, with or without a usable key (see "Record key in the lookup answer"). The `Player` SHALL contain the player UUID, name, team (if present), and affiliations in server order. UUIDs arrive as 16 raw bytes and SHALL be held as 16 bytes; an absent team SHALL be held as the nil UUID. Display names arrive as unterminated ASCII of up to 255 bytes and SHALL be held as terminated strings of at most 36 characters, truncating longer names. An absent name SHALL be held as the empty string. The device SHALL keep at most three affiliations.

#### Scenario: Player with team and affiliations
- **WHEN** the response contains a team and two affiliations
- **THEN** the `Player` reports that it has a team, with the team UUID and name
- **AND** its affiliations list has two entries in response order, each with category UUID, category name, affiliation UUID, and affiliation name

#### Scenario: Unassigned player
- **WHEN** the response omits `team_id` and `team_name` and has no affiliations
- **THEN** the `Player` reports that it has no team
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
- **THEN** the `Player` has three affiliations

#### Scenario: Affiliations past the device limit
- **WHEN** the response contains more than three affiliations
- **THEN** the response is still decoded
- **AND** the `Player` holds the first three affiliations in response order

### Requirement: Undecodable responses are reported as errors
If a successful HTTP response body cannot be decoded as `DevicePlayerResponse`, the client SHALL end the lookup as failed, holding no record key, and SHALL NOT report the player found.

#### Scenario: Garbage body
- **WHEN** the response body is bytes that are not a valid `DevicePlayerResponse`
- **THEN** the lookup ends as failed

#### Scenario: Malformed UUID
- **WHEN** the player, team, category, or affiliation ID is present but not exactly 16 bytes
- **THEN** the response is treated as undecodable

#### Scenario: Missing required UUID
- **WHEN** the player, category, or affiliation ID is absent
- **THEN** the response is treated as undecodable, even when it carries a `32`-byte key, and no key is held

## ADDED Requirements

### Requirement: Lookup errors are classified
If the HTTP layer reports an error, the client SHALL end the lookup as: not found for status `404` (no player is linked to the band, or no active event); untrusted server for `SERVER_NOT_TRUSTED`, including a request the client refuses when no trusted server is configured; failed for anything else, including `400`. Each lookup SHALL end exactly once.

#### Scenario: Unknown band
- **WHEN** the HTTP layer reports status `404` for the lookup request
- **THEN** the lookup ends as not found

#### Scenario: Untrusted server
- **WHEN** the HTTP layer reports `SERVER_NOT_TRUSTED`, or refuses the request at queue time with it
- **THEN** the lookup ends once, as untrusted server

#### Scenario: Other failures
- **WHEN** the HTTP layer reports status `400` or `500`, or a timeout
- **THEN** the lookup ends as failed

### Requirement: Record key in the lookup answer
The client SHALL keep the response's `record_key` only when it is exactly `32` bytes, and report the lookup found with a key. An absent key or one of any other length SHALL leave no key held while the profile still decodes, and the lookup SHALL report found without a usable key; such a lookup registers nobody (one delivery, #486).

#### Scenario: Thirty-two-byte key
- **WHEN** the response carries a profile and a `32`-byte `record_key`
- **THEN** the lookup reports found with a key, holding those `32` bytes

#### Scenario: Key of the wrong length or none
- **WHEN** the response carries a profile and a `31`- or `33`-byte `record_key`, or none
- **THEN** the lookup reports found without a usable key and holds no key

### Requirement: Band UID is never logged
No build SHALL log a band UID: not the lookup, and not the HTTP client logging the request's path, address or the server's error text. Logs SHALL show the lookup's path only up to `/device/players/by-band/`.

#### Scenario: Failed lookups log no UID
- **WHEN** lookups end undecodable, not found, timed out, or with a UID of the wrong length
- **THEN** no log line contains the UID, and the request's loggable path is `/device/players/by-band/`

## REMOVED Requirements

### Requirement: Transport errors are forwarded unchanged
**Reason**: The by-band lookup reports how it ended instead of forwarding errors; its "Unknown PIN" scenario has no PIN left to name.
**Migration**: "Lookup errors are classified" (404 is "Unknown band").
