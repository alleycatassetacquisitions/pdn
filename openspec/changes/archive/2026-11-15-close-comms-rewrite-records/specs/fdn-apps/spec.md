## MODIFIED Requirements

### Requirement: Boot sequence, periodic FDN beacon, and app registration

At boot, the FDN SHALL display its splash screen, begin periodic peer-to-peer identity broadcasts, and mount its home app:

- At boot, the FDN SHALL display `"ALLEYCAT"` for `2000 ms` and SHALL start one upload cycle for any stored records (`match-collection`, `crash-reporting`).
- After the splash hold, the FDN SHALL broadcast a wireless player-info beacon that identifies its sender as an FDN every `12,000 ms` whenever the peer-to-peer radio is enabled, in every app.
- The FDN SHALL register its top-level apps (`idle` and `symbol-match`), mount `idle` as the home app, and return to `idle` when connected PDNs unplug from `symbol-match`.

#### Scenario: FDN displays 2000 ms splash and starts 12-second beacon at boot
- **WHEN** an FDN completes hardware initialization at boot
- **THEN** `"ALLEYCAT"` is displayed for `2000 ms`, the `12,000 ms` beacon identifying an FDN starts and continues in every app, and the home app is mounted

### Requirement: Cabled background services on any Connected PDN

The FDN SHALL serve PDNs as follows:

- Whenever a PDN reaches the **Connected** state on either upstream serial jack and the FDN's UTC wall clock has been synchronized (`>= 2025-01-01T00:00:00Z`), the FDN SHALL propagate its current UTC epoch milliseconds to that PDN (`http-date-clock-sync`).
- While the peer-to-peer radio is enabled, the FDN SHALL accept and store match and crash record frames from any PDN, Connected on a jack or not, acknowledging each record only after it is stored (`record-relay`).

#### Scenario: Connected PDN receives UTC time and transfers pending match and crash records
- **WHEN** a provisioned PDN with uncollected match and crash records connects to an FDN whose UTC clock is synchronized
- **THEN** the FDN propagates its UTC epoch milliseconds to the PDN and stores and acknowledges the match and crash records the PDN sends on hearing the FDN's beacon
