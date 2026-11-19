## MODIFIED Requirements

### Requirement: FDN server lookup and protobuf profile decoding

When a band is tapped at an FDN while the jack of a Connected unprovisioned PDN holds the registration turn, the FDN SHALL look up the player linked to that band from the server:

- The FDN SHALL look the band up by its UID alone (`device-player-lookup`), only from a server that presents the pinned key (`fdn-server-trust`), and SHALL send no PIN and no `Authorization` header. It SHALL hold the UID only for its lookup and SHALL NOT store or log it.
- On a successful HTTP `200 OK` response (with a bounded response body size so oversized payloads are rejected before exhausting memory), the FDN SHALL decode the protobuf payload via the device-player codec:
  - The player UUID, category UUIDs, and affiliation UUIDs SHALL each be validated as exactly `16` raw bytes and non-nil (not `16` zero bytes); a missing, non-16-byte, or all-zero required UUID SHALL cause the response to be rejected as undecodable.
  - If a team is present, its team UUID SHALL be validated as `16` raw bytes and non-nil; if absent, the team UUID SHALL be recorded as the nil UUID (`16` zero bytes).
  - Display names (player name, team name, category name, and affiliation name) SHALL be stored as null-terminated ASCII strings truncated to at most `36` characters (`37`-byte buffer, from server strings up to `255` bytes); an absent name SHALL be stored as the empty string.
  - Up to `3` affiliations SHALL be retained in server response order; any additional affiliations beyond the first `3` SHALL be skipped without failing the decode.
  - A record key of exactly `32` bytes SHALL be kept for that registration only; a key of any other length SHALL be treated as no key.
- A response with no usable record key SHALL register nobody. If the HTTP response body cannot be decoded as a valid player profile, if the HTTP layer reports a transport or non-2xx error, or if no answer arrives within `15,000 ms`, the FDN SHALL report a lookup failure to the cabled PDN rather than delivering a partial profile, and SHALL give the radio back to the peer-to-peer link.

#### Scenario: FDN decodes player profile with team, affiliations, and truncated display name
- **WHEN** the server responds with a valid non-nil 16-byte player UUID, a 45-byte ASCII display name, a valid team, and 6 affiliations
- **THEN** the codec retains the 16-byte player UUID, truncates the display name to 36 ASCII characters (`37`-byte null-terminated buffer), retains the team UUID and name, and retains the first 3 affiliations in server order

#### Scenario: Missing, wrong-length, or nil required UUID in server response is rejected
- **WHEN** the server responds with a player or affiliation UUID whose length is not 16 bytes or whose 16 bytes are all zero
- **THEN** the codec rejects the response as undecodable and no profile is delivered to the PDN

## ADDED Requirements

### Requirement: FDN registration queue

The FDN SHALL queue, in arrival order, each PDN Connected on an input jack whose context reports no applied profile, learning arrivals, departures and profile changes from the jack's events. A PDN that leaves or reports an applied profile SHALL leave the queue. A PDN whose lookup ended SHALL rejoin at the back if still Connected and unprovisioned. A turn held `30,000 ms` with no tap SHALL pass to the next PDN.

#### Scenario: Second PDN waits its turn
- **WHEN** an unprovisioned PDN connects on the secondary jack while the primary jack's PDN holds the turn
- **THEN** the secondary jack's PDN is queued behind it

#### Scenario: Untapped turn passes on
- **WHEN** the turn has been with the primary jack's PDN for `30,000 ms` with no tap while the secondary jack's PDN waits
- **THEN** the turn moves to the secondary jack's PDN

### Requirement: FDN tells each jack whether its turn is open

Every context the FDN sends a PDN SHALL say whether that PDN holds the registration turn. The turn SHALL be the queue head's, by jack and MAC, and open only while a tap can count: the screen is shown, the reader answers, no lookup or `3000 ms` hold runs, and the head's MAC is the peer of no other jack. Contexts SHALL go again when the turn opens, moves or closes, and when one is given up on while its PDN stays Connected.

#### Scenario: Only the first PDN is told to tap
- **WHEN** unprovisioned PDNs are Connected on both jacks, the primary one first
- **THEN** the primary jack's PDN is told its turn is open and the secondary jack's PDN that it is closed

#### Scenario: No reader, no turn
- **WHEN** an unprovisioned PDN is Connected and the band reader does not answer
- **THEN** the FDN tells it its turn is closed

#### Scenario: Lost turn context is sent again
- **WHEN** the context that moved the turn to a PDN is given up on while that PDN stays Connected
- **THEN** the FDN sends it a fresh context carrying the open turn

#### Scenario: A MAC on both jacks gets no turn
- **WHEN** a PDN on the secondary jack claims in its HELLO the MAC of the unprovisioned PDN on the primary jack
- **THEN** the FDN tells that MAC its turn is closed, and a tap issues no lookup and writes no key on either jack

### Requirement: A tap is bound to the PDN holding the turn

A band tapped while the turn is open SHALL be looked up for the PDN holding it, identified by jack and MAC, once the context closing its turn has reached it or `500 ms` passed. A tap with no turn open, during a lookup or during the `3000 ms` result hold SHALL be ignored. Only while that MAC is still Connected on that jack SHALL the FDN write its key there and, once written, send the profile to that MAC; a failure goes only to a PDN still Connected.

#### Scenario: Key before profile
- **WHEN** the server answers a tap with a profile and a 32-byte record key
- **THEN** the FDN writes the key on the turn's jack and sends the profile to that PDN's MAC only after the key frame is written

#### Scenario: PDN swapped during the lookup gets nothing
- **WHEN** the tapped-for PDN leaves its jack during the lookup and another PDN is Connected there before the answer
- **THEN** the FDN writes no record key and sends no profile

#### Scenario: Tap during the result hold is ignored
- **WHEN** a band is tapped `1000 ms` after a lookup result
- **THEN** the FDN issues no lookup

### Requirement: Registration wire messages

The record key SHALL travel only as the jack's record-key application frame (`wireless-transport`). The profile packet SHALL carry the player UUID, name, team and up to `3` affiliations, or a lookup-failed status, and no PIN. A PDN's context SHALL say whether it has an applied profile, the demo profile included. PDN and FDN images SHALL be updated together.

#### Scenario: Lookup failure reaches the PDN
- **WHEN** the lookup for the PDN holding the turn fails and that PDN is still Connected
- **THEN** the FDN sends it the profile packet with the lookup-failed status and writes no record key
