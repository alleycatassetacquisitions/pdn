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

### Requirement: Unprovisioned PDN state and prohibition of offline or PDN Wi-Fi registration

An unprovisioned PDN SHALL wait for registration without using Wi-Fi or offering offline registration:

- On boot, a PDN SHALL initialize with no registered profile and a nil 16-byte player UUID (never generating a random player ID at boot that could masquerade as a provisioned profile). After the boot splash hold, an unprovisioned PDN SHALL remain in the provisioning app until registered or a role is chosen.
- A PDN SHALL NOT enable Wi-Fi station mode, SHALL NOT issue HTTP player-lookup requests directly, and a release build SHALL NOT offer offline registration. The PDN's first provisioning screen SHALL be the band waiting screen (PDN waiting screens); players type no PIN. Releasing and holding the primary button for `5000 ms` on it SHALL lead to role selection with the build's identity and no record key: a test build applies its MAC test identity (Test-only registration), a release build keeps the nil player UUID as "Demo", so its matches are practice matches, played but not stored (`quickdraw-match`).
- While waiting on the provisioning screen, the PDN SHALL remain eligible to receive peer-to-peer firmware distribution images and SHALL start seeding its running firmware image when the secondary button is released and held for `5000 ms` (`firmware-distribution`).

#### Scenario: Unprovisioned PDN waits for FDN without Wi-Fi or offline fallback
- **WHEN** a release-build PDN boots with no provisioned profile and no FDN connected
- **THEN** the PDN has a nil player UUID, stays in the provisioning app on the band waiting screen with the peer-to-peer radio enabled, and never offers offline registration or looks a player up itself

#### Scenario: Any PIN leads to role selection
- **WHEN** a user releases and holds the primary button for `5000 ms` on the band waiting screen, which takes no PIN
- **THEN** the PDN shows role selection, and confirming a role applies the build's identity with no record key, shows `"Welcome"` and the player's name for `5000 ms`, and enters the hub

#### Scenario: Holding secondary button for 5 seconds on provisioning screen starts firmware seeding
- **WHEN** a user releases and holds the secondary button for `5000 ms` on the PDN provisioning screen
- **THEN** the PDN enters the firmware distribution seeding state (`firmware-distribution`)

### Requirement: Cabled FDN-to-PDN profile delivery and wire identity

Once the FDN decodes a valid player profile with its record key (or a lookup failure) for a Connected PDN, the FDN SHALL deliver the result to that cabled PDN, the key over its jack and the profile or failure over a reliable peer-to-peer provisioning channel:

- An unprovisioned PDN whose session has no registered profile SHALL advertise the unregistered sentinel identity (nil 16-byte UUID, mapping to `0xFFFF` on any 16-bit wire field and never `0`) in its outbound connection context until a profile is applied.
- The PDN SHALL accept a record key, a provisioned profile or a lookup-failure notification **only while the band waiting screen or the registration attempt it started is open**, a record key **only over a jack whose Connected peer is an FDN**, and a profile or notification **only when the wireless sender MAC address matches the verified MAC address of an FDN currently Connected on a physical serial jack**. The first one accepted SHALL bind the attempt to that FDN's MAC and jack, and later ones SHALL come from both.
- Once provisioned, the PDN's connection context and peer profile exchanged with cabled neighbors SHALL carry the player's 16-byte player UUID, 16-byte team UUID, standing role (`Hunter` or `Bounty`), and display name (truncated to fit the wire profile field).

#### Scenario: Unprovisioned PDN advertises unregistered sentinel before profile delivery
- **WHEN** an unprovisioned PDN connects to a peer on a serial jack before receiving a player profile
- **THEN** the PDN's outbound connection context carries the unregistered sentinel identity (nil UUID / `0xFFFF`, never `0`)

#### Scenario: Uncabled wireless provisioning frame is rejected by PDN
- **WHEN** an unprovisioned PDN receives a wireless profile-delivery or lookup-failure frame from a sender MAC that is not Connected as an FDN on a physical serial jack
- **THEN** the PDN ignores the frame and remains on the provisioning screen

#### Scenario: Record key from a jack without a Connected FDN is ignored
- **WHEN** a PDN on the band waiting screen receives a record-key frame on a jack whose Connected peer is a PDN, or, once a profile from the FDN on its other jack started the attempt, on that other FDN's jack
- **THEN** the PDN holds no record key from it

### Requirement: Session profile application, role/allegiance mapping, and welcome flow

When an unprovisioned PDN in the provisioning app holds both a resolved player profile and the record key from its cabled FDN, accepted in either order within `10,000 ms` of the first:

- The PDN SHALL store the profile on the single session player instance, and the key as its record key (`record-authentication`):
  - A team name of `"Hunter"` SHALL set the player's standing role to `Hunter`; `"Bounty"`, an unassigned team (nil team UUID), or any unrecognized team name SHALL set the standing role to `Bounty`.
  - From the player's affiliations, an affiliation matching `"Helix*"` SHALL map to Helix, `"Endline*"` to Endline, `"Reboot*"` or `"Resistance*"` to Resistance, and `"Alleycat*"` (or any unrecognized/absent affiliation) to Alleycat.
  - Applying the profile while any serial jack is Connected SHALL trigger a live context update to connected neighbors so peers learn the PDN's newly registered standing role and identity, and an FDN that the PDN has a profile. Clearing a held profile SHALL do the same.
- After applying the profile, the PDN SHALL transition to the welcome screen displaying `"Welcome"` at `y = 24` and the player's display name at `y = 44` for `5000 ms`, and then transition into the hub app (`pdn-session-routing`), which runs the 20-step (`100 ms` per step) haptic awaken sequence before mounting the hub idle state. A profile or key arriving after that SHALL change nothing.
- If the cabled FDN reports that the player lookup failed, or the pair is not complete `10,000 ms` after its first piece, the PDN SHALL drop whatever it holds of the pair at once, display `"Unable to"` / `"Locate Asset"` for `3000 ms` and return to the provisioning screen.

#### Scenario: Applied profile maps role and allegiance and transitions through welcome to hub app
- **WHEN** an unprovisioned PDN holds the record key and a profile from its Connected FDN with display name `"Ada"`, team `"Hunter"`, and affiliation `"Helix"`
- **THEN** the session player's standing role is set to `Hunter`, allegiance is set to Helix, the PDN displays `"Welcome"` and `"Ada"` for `5000 ms`, and transitions to the hub app (`Awaken` -> `Hub Idle`)

#### Scenario: Lookup failure from cabled FDN displays asset error for 3000 ms and returns to provisioning screen
- **WHEN** a player lookup at the Connected FDN fails while the PDN is waiting in the provisioning app
- **THEN** the PDN displays `"Unable to"` / `"Locate Asset"` for `3000 ms` and returns to the provisioning screen

#### Scenario: Profile before key registers the PDN
- **WHEN** the PDN accepts the profile from its Connected FDN and then the `32`-byte record key over that FDN's jack within `10,000 ms`
- **THEN** the PDN applies the profile and shows the welcome screen

#### Scenario: One piece alone fails the attempt
- **WHEN** the PDN accepts a record key and no profile arrives within `10,000 ms`
- **THEN** the PDN drops the key at once, shows the lookup failure screen and returns to the provisioning screen unprovisioned

#### Scenario: Registered PDN leaves the FDN's queue
- **WHEN** a PDN Connected on an FDN jack applies its delivered profile
- **THEN** it sends that FDN a context marking a profile as applied, and the FDN does not give it the turn again

## ADDED Requirements

### Requirement: FDN registration queue

The FDN SHALL queue, in arrival order, each PDN Connected on an input jack whose context reports no applied profile, learning arrivals, departures and profile changes from the jack's events. A PDN that leaves or reports an applied profile SHALL leave the queue. A PDN whose lookup ended SHALL rejoin at the back if still Connected and unprovisioned. An open turn untapped `30,000 ms` after the next PDN began waiting SHALL pass to it.

#### Scenario: Second PDN waits its turn
- **WHEN** an unprovisioned PDN connects on the secondary jack while the primary jack's PDN holds the turn
- **THEN** the secondary jack's PDN is queued behind it

#### Scenario: Untapped turn passes on
- **WHEN** the secondary jack's PDN has waited `30,000 ms` behind the primary jack's open turn with no tap, however long that turn was open before
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

### Requirement: PDN waiting screens

The band waiting screen SHALL show `"Tap band"` / `"at FDN"` while an FDN Connected on a jack says in its latest context that this PDN holds its registration turn, `"Please"` / `"wait"` while an FDN is Connected otherwise, and `"Cable to"` / `"FDN"` with none. It SHALL learn each change from the jack's events, with no replug. Each time it is shown the PDN SHALL clear its session player provider (`player-provider`).

#### Scenario: Cabled PDN holding the turn asks for a band tap
- **WHEN** the FDN Connected on an unprovisioned PDN's jack sends a context saying that PDN holds the turn
- **THEN** the PDN displays `"Tap band"` / `"at FDN"`

#### Scenario: Cabled PDN without the turn waits
- **WHEN** the FDN Connected on the PDN's jack then sends a context saying the turn is closed
- **THEN** the PDN displays `"Please"` / `"wait"` with the cable still seated

#### Scenario: PDN with no FDN asks to be cabled
- **WHEN** no FDN is Connected on either of an unprovisioned PDN's jacks
- **THEN** the PDN displays `"Cable to"` / `"FDN"`

### Requirement: Random seed from the registered player

When a PDN applies a profile delivered by an FDN, it SHALL seed its random number generator from that profile's player UUID, the same on any PDN, before the welcome screen is shown. The demo path SHALL leave the default seed.

#### Scenario: Same player gives the same seed
- **WHEN** two PDNs each apply a delivered profile with the same player UUID
- **THEN** both seed their random number generators with the same value
