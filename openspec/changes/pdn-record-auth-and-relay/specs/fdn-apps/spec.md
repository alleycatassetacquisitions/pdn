## MODIFIED Requirements

### Requirement: Boot sequence, periodic FDN beacon, and app registration

At boot, the FDN SHALL display its splash screen, begin periodic peer-to-peer identity broadcasts, and mount its home app:

- At boot, the FDN SHALL display `"ALLEYCAT"` for `2000 ms` while allowing any boot-time crash report upload to progress.
- After the splash hold, the FDN SHALL broadcast a wireless FDN player-info beacon every `12,000 ms` whenever the peer-to-peer radio is enabled. The beacon SHALL identify its sender as an FDN (`record-relay`).
- The FDN SHALL register its top-level apps (`idle` and `symbol-match`), mount `idle` as the home app, and return to `idle` when connected PDNs unplug from `symbol-match`.

#### Scenario: FDN displays 2000 ms splash and starts 12-second beacon at boot
- **WHEN** an FDN completes hardware initialization at boot
- **THEN** `"ALLEYCAT"` is displayed for `2000 ms`, the `12,000 ms` periodic wireless FDN player-info broadcast starts, and the home app is mounted

#### Scenario: Beacon continues while a PDN is Connected or the symbol-match app is mounted
- **WHEN** the FDN's peer-to-peer radio is enabled and a PDN is Connected on one of its serial jacks or the `symbol-match` app is mounted
- **THEN** the FDN still broadcasts its beacon every `12,000 ms`, and the beacon identifies its sender as an FDN

### Requirement: Background services for Connected and nearby PDNs

The FDN SHALL propagate its clock to every Connected PDN and SHALL collect records from any PDN:

- Whenever a PDN reaches the **Connected** state on either of the FDN's upstream serial jacks: if the FDN's UTC wall clock has been synchronized (`>= 2025-01-01T00:00:00Z`), the FDN SHALL propagate its current UTC epoch milliseconds to the Connected PDN (`http-date-clock-sync`).
- The FDN SHALL collect match records (`match-collection`) and crash records (`crash-reporting`) from any PDN as `record-relay` specifies. Collecting a record SHALL NOT depend on the sending PDN being Connected on a serial jack.

#### Scenario: Connected PDN receives UTC time and transfers pending match and crash records
- **WHEN** a provisioned PDN with uncollected match and crash records connects to an FDN whose UTC clock is synchronized
- **THEN** the FDN propagates its UTC epoch milliseconds to the PDN and collects the PDN's pending match and crash records (`record-relay`)

#### Scenario: Clock is not propagated to a PDN that is not Connected
- **WHEN** a PDN that is not Connected on any serial jack of the FDN sends a pending crash record to that FDN
- **THEN** the FDN does not propagate its clock to that PDN; collecting the record follows `record-relay`

### Requirement: Registration screen for a cabled unprovisioned PDN

When a PDN whose connection context shows no applied profile (unprovisioned, as distinct from a demo player) becomes Connected on an FDN serial jack while the FDN is in the idle or player-detected state, the FDN SHALL show a registration screen ahead of any other cabled flow:

- While waiting for a lookup request, the FDN SHALL display `"REGISTER"` / `"ENTER CODE"`.
- While a lookup for that PDN is in progress, the FDN SHALL display `"LOOKING"` / `"UP"`.
- On a delivered profile, the FDN SHALL display `"WELCOME"` and the player's display name for `3000 ms`; on a lookup failure or timeout, `"UNABLE TO"` / `"LOCATE ASSET"` for `3000 ms`.
- When a lookup returned a profile and no record key the FDN could deliver to the PDN (`player-provisioning`), the FDN SHALL display `"UNABLE TO"` / `"ISSUE KEY"` for `3000 ms` in place of `"UNABLE TO"` / `"LOCATE ASSET"`.
- The FDN SHALL end a lookup that has not answered within `8000 ms` as a lookup failure.
- Outside the registration screen, the FDN SHALL drop a lookup request while a registered PDN is Connected on an FDN jack, so a lookup's Wi-Fi excursion never interrupts that PDN's symbol-match game.
- The FDN SHALL leave the registration screen for the idle state when the PDN is unplugged, when a result has been shown, or, while waiting for a request, when the cabled PDN's context shows an applied profile.

#### Scenario: Unprovisioned PDN plugged in shows the registration screen
- **WHEN** an unprovisioned PDN becomes Connected on an idle FDN
- **THEN** the FDN displays `"REGISTER"` / `"ENTER CODE"` instead of entering the symbol-match flow

#### Scenario: Successful lookup shows the player's name
- **WHEN** the FDN delivers a profile named `"Ada"` to the cabled PDN
- **THEN** the FDN displays `"WELCOME"` / `"Ada"` for `3000 ms` and returns to the idle state

#### Scenario: Lookup that yields no record key shows that no key was issued
- **WHEN** the lookup for a cabled unprovisioned PDN returns a valid profile and no record key
- **THEN** the FDN displays `"UNABLE TO"` / `"ISSUE KEY"` for `3000 ms`, delivers no profile, and returns to the idle state

## RENAMED Requirements

- FROM: `### Requirement: Cabled background services on any Connected PDN`
- TO: `### Requirement: Background services for Connected and nearby PDNs`
