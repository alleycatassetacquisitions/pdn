## ADDED Requirements

### Requirement: PDN registers only through a cabled FDN

On boot a PDN SHALL hold no registered profile and a nil 16-byte player UUID, and after the splash hold an unprovisioned PDN SHALL show the waiting screen. It SHALL register only from a profile and record key delivered by an FDN Connected on one of its jacks, and SHALL send no message to ask for them. It SHALL NOT enable Wi-Fi station mode, SHALL NOT issue HTTP requests, and SHALL NOT offer offline registration.

#### Scenario: Unprovisioned PDN waits on the waiting screen without Wi-Fi
- **WHEN** a PDN boots with no provisioned profile and no FDN connected
- **THEN** the PDN has a nil player UUID, shows the waiting screen with the peer-to-peer radio enabled, and never enables Wi-Fi or offers offline registration

#### Scenario: Cabled PDN asks for nothing
- **WHEN** an unprovisioned PDN becomes Connected to an FDN on one of its jacks
- **THEN** the PDN sends no registration request and waits for that FDN to deliver a record key and a profile

### Requirement: PDN waiting screens

While unprovisioned, the waiting screen SHALL show `"Cable to"` / `"FDN"` when no FDN is Connected on either jack, and `"Tap band"` / `"at FDN"` while one is. On a lookup-failure notification, or when a registration attempt fails, the PDN SHALL show `"Unable to"` / `"Locate Asset"` for `3000 ms` and then the waiting screen again. Each time the waiting screen is shown the PDN SHALL clear its session player provider (`player-provider`).

#### Scenario: PDN with no FDN cabled asks to be cabled
- **WHEN** an unprovisioned PDN shows the waiting screen with no FDN Connected on either jack
- **THEN** it displays `"Cable to"` / `"FDN"`

#### Scenario: Cabled PDN asks for a band tap
- **WHEN** an FDN becomes Connected on one of an unprovisioned PDN's jacks
- **THEN** the PDN displays `"Tap band"` / `"at FDN"`

#### Scenario: Lookup failure returns to the waiting screen
- **WHEN** a PDN on the waiting screen receives the lookup-failure notification from the FDN Connected on its jack
- **THEN** it displays `"Unable to"` / `"Locate Asset"` for `3000 ms`, clears its session player provider, and shows the waiting screen again

### Requirement: Firmware distribution from the waiting screen

While the waiting screen is shown, the PDN SHALL be eligible to receive peer-to-peer firmware distribution images, and SHALL start seeding its running firmware image when the secondary button is released and held for `5000 ms` (`firmware-distribution`). When seeding ends the PDN SHALL return to the waiting screen.

#### Scenario: Holding the secondary button for 5 seconds on the waiting screen starts firmware seeding
- **WHEN** a user releases and holds the secondary button for `5000 ms` on the PDN waiting screen
- **THEN** the PDN enters the firmware distribution seeding state (`firmware-distribution`)

#### Scenario: Seeding returns to the waiting screen
- **WHEN** a PDN that entered seeding from the waiting screen finishes seeding
- **THEN** it shows the waiting screen with no registered profile

### Requirement: Demo path from the waiting screen

Releasing and holding the primary button for `5000 ms` on the waiting screen SHALL offer the demo role picker (Hunter or Bounty) and then enter the hub app. A demo player has no server player UUID and holds no record key. Its connection context SHALL mark a profile as applied, so an FDN does not queue it for registration.

#### Scenario: Primary hold enters the hub without a server identity
- **WHEN** a user releases and holds the primary button for `5000 ms` on the waiting screen and picks Hunter
- **THEN** the PDN enters the hub app as a Hunter with a nil player UUID and no record key

#### Scenario: Demo PDN is not queued at an FDN
- **WHEN** a PDN on the demo profile becomes Connected on an FDN jack
- **THEN** the FDN queues no registration for that jack

### Requirement: Random seed from the registered player

When a PDN applies a profile delivered by an FDN, it SHALL seed its random number generator from that profile's player UUID before the welcome screen is shown. The demo path SHALL leave the default seed.

#### Scenario: Same player gives the same seed
- **WHEN** two PDNs each apply a profile with the same player UUID
- **THEN** both seed their random number generators with the same value

### Requirement: PDN accepts a registration only from its jack's FDN

On the waiting screen a PDN SHALL accept a profile or lookup-failure notification only from the FDN Connected on one of its jacks, and a record key only over that jack. The first profile or record key it accepts SHALL start a registration attempt, which SHALL fail if the PDN does not hold both within `10,000 ms`, on a lookup-failure notification, or when that FDN leaves the jack. The PDN SHALL apply the profile only with both held.

#### Scenario: Key then profile registers the PDN
- **WHEN** a PDN on the waiting screen accepts a `32`-byte record key over the jack of its Connected FDN and then the profile from that FDN within `10,000 ms`
- **THEN** the PDN applies the profile and shows the welcome screen

#### Scenario: Profile with no key in time fails the attempt
- **WHEN** a PDN accepts a profile from the FDN on its jack and accepts no record key within `10,000 ms`
- **THEN** the PDN drops the profile, shows the lookup failure screen and returns to the waiting screen unprovisioned

#### Scenario: Profile from an FDN that is not on its jack is ignored
- **WHEN** a PDN on the waiting screen receives a profile from an FDN that is not Connected on either of its jacks
- **THEN** the PDN ignores it and starts no registration attempt

#### Scenario: FDN leaving the jack ends the attempt at once
- **WHEN** the FDN leaves the jack after the PDN accepted its record key and before the profile arrived
- **THEN** the PDN drops the key, shows the lookup failure screen and returns to the waiting screen

### Requirement: Registration after a restart

After a restart a PDN SHALL show the waiting screen with no registered profile, no record key and a totals baseline of `0`. Registration SHALL be reachable only from the waiting screen, and a running PDN SHALL offer no way back to it.

#### Scenario: Restart returns the PDN to the waiting screen with nothing held
- **WHEN** a PDN registered to a player is restarted
- **THEN** the PDN shows the waiting screen with a nil player UUID, no record key and a totals baseline of `0`

### Requirement: FDN registration queue

When a PDN with no applied profile becomes Connected on an FDN jack, the FDN SHALL queue a registration for that jack. One SHALL be pending at a time, in arrival order, and only while the registration screen is shown (`fdn-apps`). A pending registration with no tap SHALL end after `30,000 ms`. When a registration ends, whatever its outcome, its PDN SHALL requeue at the back if still Connected with no applied profile. A PDN that leaves its jack or shows an applied profile SHALL leave the queue.

#### Scenario: Second PDN waits its turn
- **WHEN** an unprovisioned PDN connects on the secondary jack while a registration for the primary jack is pending
- **THEN** the secondary jack's registration is queued behind it and becomes pending when the primary jack's registration ends

#### Scenario: Untapped registration goes to the back of the queue
- **WHEN** a registration has been pending for `30,000 ms` with no tap while another PDN is queued and its PDN is still Connected
- **THEN** the other PDN's registration becomes pending and the first is queued behind it

#### Scenario: Unplugged PDN leaves the queue
- **WHEN** a queued PDN is unplugged before its registration becomes pending
- **THEN** the FDN removes that registration from the queue

#### Scenario: Delivered profile the PDN did not apply requeues
- **WHEN** the FDN delivers a profile and the PDN's context still shows no applied profile when that registration ends
- **THEN** the FDN queues that PDN's registration again at the back

#### Scenario: PDN that applies a profile leaves the queue
- **WHEN** a queued PDN's context comes to show an applied profile
- **THEN** the FDN removes that registration from the queue

#### Scenario: Nothing is pending during symbol match
- **WHEN** an unprovisioned PDN is queued while symbol match is running
- **THEN** no registration is pending, no `30,000 ms` timer runs and a tap issues no lookup until the registration screen is shown

### Requirement: A tap is bound to the pending jack

A band read while a registration is pending and no lookup is running SHALL be looked up for the PDN Connected on the pending jack at the tap, identified by its MAC address. The FDN SHALL write the record key on that jack only while that MAC is still the PDN Connected there, and send the profile or lookup-failure notification to that MAC. A tap with nothing pending, during a lookup, or within `2000 ms` after the pending registration passes to the other jack SHALL be ignored.

#### Scenario: Tap registers the PDN on the pending jack
- **WHEN** a linked band is tapped while the primary jack's registration is pending
- **THEN** the FDN looks the band up and delivers the record key on the primary jack and the profile to the MAC address of the PDN Connected there

#### Scenario: Tap with nothing pending is ignored
- **WHEN** a band is tapped while no registration is pending
- **THEN** the FDN issues no lookup

#### Scenario: Tap during a lookup is ignored
- **WHEN** a second band is tapped while the first tap's lookup is running
- **THEN** the FDN issues no second lookup and the first registration continues

#### Scenario: PDN swapped on the jack during the lookup gets no key
- **WHEN** the PDN bound at the tap leaves the pending jack during the lookup and another PDN becomes Connected there before the answer
- **THEN** the FDN writes no record key on that jack and delivers no profile

#### Scenario: Tap just after the pending jack changes is ignored
- **WHEN** a band is tapped `1000 ms` after the pending registration passed from the primary to the secondary jack
- **THEN** the FDN issues no lookup and the secondary jack's registration stays pending

### Requirement: FDN band lookup

The FDN SHALL look up a tapped band by its UID alone (`device-player-lookup`) and SHALL send no lookup for a UID that is not `4` or `7` bytes. The decoding, record key and failure rules of "FDN server lookup and protobuf profile decoding" SHALL apply to the band lookup unchanged. The FDN SHALL hold a UID only for its lookup and SHALL NOT store it.

#### Scenario: Seven-byte band is looked up
- **WHEN** a band with a `7`-byte UID is tapped for a pending registration
- **THEN** the FDN issues the device-player lookup for that UID

#### Scenario: UID of another length is not looked up
- **WHEN** the reader reports a `10`-byte UID for a pending registration
- **THEN** the FDN issues no lookup and the registration stays pending

### Requirement: Registration wire messages

The registration request packet type SHALL be retired and its type number (`26`) SHALL stay reserved. The profile packet SHALL carry no PIN. The FDN SHALL send the record key on the pending registration's jack before the profile, and a lookup failure SHALL reach that PDN as the profile packet's lookup-failed status. PDN and FDN images SHALL be updated together.

#### Scenario: Retired request type is dropped
- **WHEN** an FDN receives a packet of type `26`
- **THEN** it ignores the packet and issues no lookup

#### Scenario: Lookup failure reaches the PDN on the pending jack
- **WHEN** the lookup for a pending registration fails
- **THEN** the FDN sends the profile packet with the lookup-failed status to the MAC address of the PDN on the pending jack and sends no record key

### Requirement: Staff registration by staff card

A staff card SHALL be a band to the FDN: the FDN SHALL look it up and deliver its result exactly as for a player's band, and SHALL have no operator mode, chooser or button action for registration. Which player a staff card registers SHALL be decided at the desk on the server, not on the FDN.

#### Scenario: Staff card registers the player linked at the desk
- **WHEN** staff tap a staff card that the desk has linked to player "Ada" while a registration is pending
- **THEN** the FDN delivers Ada's record key and profile to the PDN on the pending jack, as for Ada's own band

## REMOVED Requirements

### Requirement: Unprovisioned PDN state and prohibition of offline or PDN Wi-Fi registration

**Reason**: PIN entry on the PDN, the PIN sent to the FDN and the PIN `0000` demo path are removed. A player registers by tapping their band at the FDN while their PDN is cabled to it, and the PDN sends nothing to register.

**Migration**: Boot state and the ban on PDN Wi-Fi and offline registration move to "PDN registers only through a cabled FDN"; the screens to "PDN waiting screens"; firmware distribution to "Firmware distribution from the waiting screen"; the demo path to "Demo path from the waiting screen" (`5000 ms` primary hold). Stale-reply rejection is dropped: the PDN sends no request to match a reply against. The PIN stays a server-side identifier.
