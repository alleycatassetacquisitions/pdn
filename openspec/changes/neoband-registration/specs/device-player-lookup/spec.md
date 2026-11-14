## MODIFIED Requirements

### Requirement: Lookup uses the protobuf device endpoint
The client SHALL queue a `GET` request to `/device/players/by-band/{uid}` with no request body, where `{uid}` is the band UID as upper-case hexadecimal with no separators: `8` characters for a `4`-byte UID, `14` for a `7`-byte UID. The client SHALL NOT look a player up by PIN. Beyond its length, the UID is not validated on the client; the server rejects malformed UIDs.

#### Scenario: Request shape
- **WHEN** a lookup is requested for the `4`-byte UID `04 A2 1B 9C`
- **THEN** the queued request has method `GET`, path `/device/players/by-band/04A21B9C`, and an empty payload

#### Scenario: Seven-byte UID
- **WHEN** a lookup is requested for the `7`-byte UID `04 5E 21 7A 3B 6C 80`
- **THEN** the queued request has path `/device/players/by-band/045E217A3B6C80`

### Requirement: Transport errors are forwarded unchanged
If the HTTP layer reports an error, including a non-2xx status such as 404 for a band no player is linked to or 400 for a malformed UID, the client SHALL pass the `WirelessErrorInfo` to the error callback without modification.

#### Scenario: Unknown PIN
- **WHEN** the HTTP layer reports an error for the lookup request
- **THEN** the error callback receives the same `WirelessErrorInfo`
