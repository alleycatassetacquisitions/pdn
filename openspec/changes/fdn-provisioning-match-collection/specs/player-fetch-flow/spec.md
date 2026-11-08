## MODIFIED Requirements

### Requirement: Fetch user only performs device player lookup
Fetch user SHALL obtain the player through `player-provisioning`, show loading
glyphs while waiting, and time out 10 s after mounting without a reply. It SHALL NOT use Wi-Fi, SHALL NOT upload matches, and SHALL NOT branch
on test or debug PIN constants.

#### Scenario: Lookup requested
- **WHEN** fetch user mounts with PIN `"1234"` while cabled to an FDN
- **THEN** the FDN receives a lookup request for `"1234"` and issues `GET /device/players/by-pin/1234`

### Requirement: Fetch failure shows asset page zero then returns to pin entry
On a lookup failure reply or fetch timeout, fetch user SHALL display only the
first confirm-offline screen ("Unable to" / "Locate Asset"), SHALL wait
3000 ms, and SHALL transition to pin entry.

#### Scenario: Lookup failure
- **WHEN** the cabled FDN replies with a lookup failure
- **THEN** page zero is shown
- **AND** after 3000 ms the app transitions to pin entry

#### Scenario: No FDN answers
- **WHEN** the fetch timer expires before any reply, including when no FDN is cabled
- **THEN** page zero is shown
- **AND** after 3000 ms the app transitions to pin entry
