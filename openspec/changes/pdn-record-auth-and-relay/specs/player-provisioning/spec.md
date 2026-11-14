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
- The response MAY carry the record key for the Connected PDN and the looked-up player (`record-authentication`):
  - The FDN SHALL accept no record key from a lookup that was not made over HTTPS with the server's certificate verified.
  - The FDN SHALL discard a record key that is not exactly `32` bytes, and SHALL still deliver the profile.
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
- **WHEN** the server responds with a valid profile and a record key that is `31` bytes long
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
- When the FDN holds a record key for the Connected PDN, the FDN SHALL deliver the record key to that PDN over the serial jack only, before it sends the profile, and SHALL NOT send a record key over the peer-to-peer radio.
- The PDN SHALL accept a record key only over a serial jack on which an FDN is Connected, and only while mounted in the provisioning app after it has sent that FDN its PIN. The PDN SHALL discard a record key whose frame fails its integrity check or that is not exactly `32` bytes. A record key accepted later in the same session replaces the one held, and the PDN drops its record key when it leaves registration without a profile applied (`record-authentication`).
- Once provisioned, the PDN's connection context and peer profile exchanged with cabled neighbors SHALL carry the player's 16-byte player UUID, 16-byte team UUID, standing role (`Hunter` or `Bounty`), and display name (truncated to fit the wire profile field).

#### Scenario: Unprovisioned PDN advertises unregistered sentinel before profile delivery
- **WHEN** an unprovisioned PDN connects to a peer on a serial jack before receiving a player profile
- **THEN** the PDN's outbound connection context carries the unregistered sentinel identity (nil UUID / `0xFFFF`, never `0`)

#### Scenario: Uncabled wireless provisioning frame is rejected by PDN
- **WHEN** an unprovisioned PDN receives a wireless profile-delivery or lookup-failure frame from a sender MAC that is not Connected as an FDN on a physical serial jack
- **THEN** the PDN ignores the frame and remains on the provisioning screen

#### Scenario: Record key is delivered over the serial jack
- **WHEN** a PDN holding no record key is mounted in the provisioning app and Connected to an FDN on a serial jack, has sent that FDN its PIN, and the lookup response carries a profile and a `32`-byte record key
- **THEN** the FDN delivers the record key over that serial jack before it sends the profile, and not over the peer-to-peer radio, and the PDN holds that record key

#### Scenario: Record key offered over the peer-to-peer radio is ignored
- **WHEN** a PDN holding no record key, mounted in the provisioning app, receives a record key over the peer-to-peer radio from any sender, including an FDN Connected on one of its serial jacks
- **THEN** the PDN ignores it and still holds no record key

#### Scenario: Record key arriving before the PDN has asked to be registered is ignored
- **WHEN** a PDN mounted in the provisioning app receives a record key over a serial jack from a Connected FDN to which it has sent no PIN
- **THEN** the PDN ignores it and still holds no record key

#### Scenario: Damaged record key is discarded by the PDN
- **WHEN** a PDN that has sent its PIN to a Connected FDN receives over the serial jack a record key whose frame fails its integrity check, or a record key that is `31` bytes long
- **THEN** the PDN discards it and holds no record key

#### Scenario: Record key is dropped when no profile follows it
- **WHEN** a PDN accepts a record key over the serial jack and then receives the lookup-failure notification, or no profile within its wait for the FDN's reply
- **THEN** the PDN returns to the registration screen and holds no record key

#### Scenario: Lookup response with no record key still registers the PDN
- **WHEN** a PDN is registered at a Connected FDN and the lookup response carries a profile and no record key
- **THEN** the FDN delivers the profile and sends no record key, and the PDN is provisioned and holds no record key

## ADDED Requirements

### Requirement: FDN delay after an unknown PIN

After a lookup the server answers with HTTP `404`, an FDN SHALL make no lookup for a delay: `2,000 ms` after the first `404`, doubling with each further `404` up to `120,000 ms`, and none again once `600,000 ms` pass without a `404`. During a delay the FDN SHALL answer a PIN request with the lookup-failure notification without contacting the server. A successful lookup SHALL NOT change the delay. The delay state SHALL be FDN-wide and held in volatile memory.

#### Scenario: First 404 starts a 2,000 ms delay
- **WHEN** the server answers an FDN's lookup with HTTP `404` and no `404` was received in the `600,000 ms` before it
- **THEN** the FDN makes no lookup for `2,000 ms`

#### Scenario: Each further 404 doubles the delay
- **WHEN** the server answers an FDN's lookups with HTTP `404` a second, third and fourth time, each within `600,000 ms` of the one before
- **THEN** the FDN makes no lookup for `4,000 ms`, `8,000 ms` and `16,000 ms` after them in turn

#### Scenario: The delay stops growing at 120,000 ms
- **WHEN** the server answers an FDN's lookup with HTTP `404` for the seventh time or more, each within `600,000 ms` of the one before
- **THEN** the FDN makes no lookup for `120,000 ms`

#### Scenario: PIN request during a delay is answered without contacting the server
- **WHEN** a PDN Connected on an FDN's jack sends a PIN while a delay is running on that FDN
- **THEN** the FDN sends the PDN the lookup-failure notification, sends no request to the server, and the delay neither restarts nor grows

#### Scenario: Lookups resume when the delay ends
- **WHEN** a delay has ended on an FDN and a Connected PDN sends a PIN
- **THEN** the FDN issues the lookup to the server

#### Scenario: The delay returns to none after 600,000 ms without a 404
- **WHEN** `600,000 ms` pass after an FDN's last HTTP `404` and the server then answers a lookup with HTTP `404`
- **THEN** the FDN makes no lookup for `2,000 ms`

#### Scenario: A successful lookup changes nothing
- **WHEN** the server answers an FDN's lookups with HTTP `404` three times, then with a profile, then with HTTP `404`, each within `600,000 ms` of the one before
- **THEN** the FDN makes no lookup for `16,000 ms` after the last `404`

#### Scenario: A lookup that fails for another reason is not counted
- **WHEN** an FDN's lookup fails with a transport error, a timeout or HTTP `503`
- **THEN** the FDN reports a lookup failure to the PDN and no delay starts or grows

#### Scenario: The delay applies to every Connected PDN
- **WHEN** a delay is running on an FDN after PINs sent by one PDN and a second PDN Connected on its other jack sends a PIN
- **THEN** the FDN sends the second PDN the lookup-failure notification and sends no request to the server

#### Scenario: Restarting the FDN clears the delay
- **WHEN** an FDN restarts while a delay is running
- **THEN** no delay is running after the restart and the next HTTP `404` starts a `2,000 ms` delay
