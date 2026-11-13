## MODIFIED Requirements

### Requirement: FDN server lookup and protobuf profile decoding

When a player's credential is presented at an FDN while a PDN is Connected on a serial jack, the FDN SHALL validate the credential and look up the player's registration from the server:

- The FDN SHALL validate that the scanned credential consists only of permitted URL-safe credential characters (such as a 4-digit ASCII PIN `[0-9]{4}`) before issuing an HTTPS `GET` request to the server's device-player lookup endpoint.
- The lookup request SHALL additionally name the Connected PDN's 6-byte MAC address.
- On a successful HTTP `200 OK` response (with a bounded response body size so oversized payloads are rejected before exhausting memory), the FDN SHALL decode the protobuf payload via the device-player codec:
  - The player UUID, category UUIDs, and affiliation UUIDs SHALL each be validated as exactly `16` raw bytes and non-nil (not `16` zero bytes); a missing, non-16-byte, or all-zero required UUID SHALL cause the response to be rejected as undecodable.
  - If a team is present, its team UUID SHALL be validated as `16` raw bytes and non-nil; if absent, the team UUID SHALL be recorded as the nil UUID (`16` zero bytes).
  - Display names (player name, team name, category name, and affiliation name) SHALL be stored as null-terminated ASCII strings truncated to at most `31` characters (`32`-byte buffer, from server strings up to `255` bytes); an absent name SHALL be stored as the empty string.
  - Up to `5` affiliations SHALL be retained in server response order; any additional affiliations beyond the first `5` SHALL be skipped without failing the decode.
  - If initial player statistics (career wins, losses, matches played, win streak, or reaction-time history) are provided, they SHALL be retained in the decoded profile; otherwise, session statistics SHALL initialize to `0`.
- The response MAY carry a record key together with its key ID for the Connected PDN (`record-authentication`):
  - The FDN SHALL accept a record key only from a lookup made over HTTPS with the server's certificate verified.
  - The FDN SHALL discard a record key that is not exactly `32` bytes or whose key ID is `0`, and SHALL still deliver the profile.
  - The FDN SHALL hold a record key in volatile memory only, SHALL NOT log, display, or store it, and SHALL erase it as soon as it has been written to the serial jack or the PDN disconnects.
- If the HTTP response body cannot be decoded as a valid player profile, or if the HTTP layer reports a transport or non-2xx error, the FDN SHALL report a lookup failure to the cabled PDN rather than delivering a partial profile.

#### Scenario: FDN decodes player profile with team, affiliations, and truncated display name
- **WHEN** the server responds with a valid non-nil 16-byte player UUID, a 45-byte ASCII display name, a valid team, and 6 affiliations
- **THEN** the codec retains the 16-byte player UUID, truncates the display name to 31 ASCII characters (`32`-byte null-terminated buffer), retains the team UUID and name, and retains the first 5 affiliations in server order

#### Scenario: Missing, wrong-length, or nil required UUID in server response is rejected
- **WHEN** the server responds with a player or affiliation UUID whose length is not 16 bytes or whose 16 bytes are all zero
- **THEN** the codec rejects the response as undecodable and no profile is delivered to the PDN

#### Scenario: Lookup names the Connected PDN
- **WHEN** a credential is presented at an FDN while a PDN is Connected on a serial jack
- **THEN** the lookup request names that PDN's 6-byte MAC address

#### Scenario: Malformed record key is discarded and the profile still delivered
- **WHEN** the server responds with a valid profile and a record key that is `31` bytes long, or a record key whose key ID is `0`
- **THEN** the FDN discards the record key, delivers the profile to the Connected PDN, and sends no record key

#### Scenario: Record key from a lookup without a verified server certificate is not accepted
- **WHEN** a lookup response that was not obtained over HTTPS with the server's certificate verified carries a record key
- **THEN** the FDN does not accept the record key and sends none to the PDN

#### Scenario: PDN disconnect before delivery erases the held record key
- **WHEN** the FDN holds a record key from the lookup response and the PDN disconnects from the serial jack before the record key has been written to that jack
- **THEN** the FDN erases the record key and retains no copy of it

### Requirement: Cabled FDN-to-PDN profile delivery and wire identity

Once the FDN decodes a valid player profile (or a lookup failure) for a Connected PDN, the FDN SHALL deliver the result to that cabled PDN over a reliable peer-to-peer provisioning channel:

- An unprovisioned PDN whose session has no registered profile SHALL advertise the unregistered sentinel identity (nil 16-byte UUID, mapping to `0xFFFF` on any 16-bit wire field and never `0`) in its outbound connection context until a profile is applied.
- The PDN SHALL accept a provisioned profile or lookup-failure notification **only while mounted in the provisioning app** and **only when the wireless sender MAC address matches the verified MAC address of an FDN currently Connected on a physical serial jack** (ignoring wireless provisioning frames from any uncabled sender or after the PDN has entered gameplay).
- When the FDN holds a record key for the Connected PDN, the FDN SHALL deliver the record key and its key ID to that PDN over the serial jack only, before it sends the profile, and SHALL NOT send a record key over the peer-to-peer radio.
- The PDN SHALL accept a record key only over a serial jack on which an FDN is Connected, and only while mounted in the provisioning app after it has sent that FDN its PIN. The PDN SHALL discard a record key that arrives damaged, is not exactly `32` bytes, or carries key ID `0`. An accepted record key replaces any the PDN holds (`record-authentication`).
- Once provisioned, the PDN's connection context and peer profile exchanged with cabled neighbors SHALL carry the player's 16-byte player UUID, 16-byte team UUID, standing role (`Hunter` or `Bounty`), and display name (truncated to fit the wire profile field).

#### Scenario: Unprovisioned PDN advertises unregistered sentinel before profile delivery
- **WHEN** an unprovisioned PDN connects to a peer on a serial jack before receiving a player profile
- **THEN** the PDN's outbound connection context carries the unregistered sentinel identity (nil UUID / `0xFFFF`, never `0`)

#### Scenario: Uncabled wireless provisioning frame is rejected by PDN
- **WHEN** an unprovisioned PDN receives a wireless profile-delivery or lookup-failure frame from a sender MAC that is not Connected as an FDN on a physical serial jack
- **THEN** the PDN ignores the frame and remains on the provisioning screen

#### Scenario: Record key is delivered over the serial jack
- **WHEN** a PDN with key ID `0` is mounted in the provisioning app and Connected to an FDN on a serial jack, and the lookup response carries a profile and a record key with key ID `1`
- **THEN** the FDN delivers the record key and key ID `1` over that serial jack and not over the peer-to-peer radio, the PDN accepts them, and the PDN's key ID is `1`

#### Scenario: Record key offered over the peer-to-peer radio is ignored
- **WHEN** a PDN with key ID `0`, mounted in the provisioning app, receives a record key over the peer-to-peer radio from any sender, including an FDN Connected on one of its serial jacks
- **THEN** the PDN ignores it and its key ID stays `0`

#### Scenario: Record key arriving before the PDN has asked to be registered is ignored
- **WHEN** a PDN mounted in the provisioning app receives a record key over a serial jack from a Connected FDN to which it has sent no PIN
- **THEN** the PDN ignores it and keeps the record key it held, if any

#### Scenario: Damaged record key is discarded by the PDN
- **WHEN** a PDN that has sent its PIN to a Connected FDN receives over the serial jack a record key that arrives damaged, or is `31` bytes long, or carries key ID `0`
- **THEN** the PDN discards it and keeps the record key it held, if any

#### Scenario: Registering again delivers a new record key
- **WHEN** a PDN holding a record key with key ID `3` is registered at a Connected FDN and the lookup response carries a profile and a record key with key ID `4`
- **THEN** the FDN delivers the profile and, over the serial jack, the record key and key ID `4`, and the PDN's key ID is `4`

#### Scenario: Lookup response with no record key leaves the PDN's key unchanged
- **WHEN** a PDN holding a record key with key ID `3` is registered at a Connected FDN and the lookup response carries a profile and no record key
- **THEN** the FDN delivers the profile and sends no record key, and the PDN's key ID stays `3`
