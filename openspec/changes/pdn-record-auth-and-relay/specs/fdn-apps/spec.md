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
- **WHEN** a provisioned PDN holding a record key, with uncollected match and crash records, connects to an FDN whose UTC clock is synchronized
- **THEN** the FDN propagates its UTC epoch milliseconds to the PDN and collects the PDN's pending match and crash records (`record-relay`)

#### Scenario: Clock is not propagated to a PDN that is not Connected
- **WHEN** a PDN that is not Connected on any serial jack of the FDN sends a pending crash record to that FDN
- **THEN** the FDN does not propagate its clock to that PDN; collecting the record follows `record-relay`

## RENAMED Requirements

- FROM: `### Requirement: Cabled background services on any Connected PDN`
- TO: `### Requirement: Background services for Connected and nearby PDNs`
