## ADDED Requirements

### Requirement: Registration screen for a cabled unprovisioned PDN

When a PDN whose connection context shows no applied profile (unprovisioned, as distinct from a demo player) becomes Connected on an FDN serial jack while the FDN is in the idle or player-detected state, the FDN SHALL show a registration screen ahead of any other cabled flow:

- While waiting for a lookup request, the FDN SHALL display `"REGISTER"` / `"ENTER CODE"`.
- While a lookup for that PDN is in progress, the FDN SHALL display `"LOOKING"` / `"UP"`.
- On a delivered profile, the FDN SHALL display `"WELCOME"` and the player's display name for `3000 ms`; on a lookup failure or timeout, `"UNABLE TO"` / `"LOCATE ASSET"` for `3000 ms`.
- The FDN SHALL end a lookup that has not answered within `8000 ms` as a lookup failure.
- Outside the registration screen, the FDN SHALL drop a lookup request while a registered PDN is Connected on an FDN jack, so a lookup's Wi-Fi excursion never interrupts that PDN's symbol-match game.
- The FDN SHALL leave the registration screen for the idle state when the PDN is unplugged, when a result has been shown, or, while waiting for a request, when the cabled PDN's context shows an applied profile.

#### Scenario: Unprovisioned PDN plugged in shows the registration screen
- **WHEN** an unprovisioned PDN becomes Connected on an idle FDN
- **THEN** the FDN displays `"REGISTER"` / `"ENTER CODE"` instead of entering the symbol-match flow

#### Scenario: Successful lookup shows the player's name
- **WHEN** the FDN delivers a profile named `"Ada"` to the cabled PDN
- **THEN** the FDN displays `"WELCOME"` / `"Ada"` for `3000 ms` and returns to the idle state

## MODIFIED Requirements

### Requirement: Periodic Wi-Fi uplink for collected matches and crash logs

Collected PDN match records (`match-collection`) and local or relayed PDN crash records (`crash-reporting`) SHALL be persisted in non-volatile storage and uploaded periodically to the central server over Wi-Fi:

- While in the idle state with no PDN actively connected, the FDN SHALL run a `300,000 ms` (5-minute) upload check timer. When the timer expires and the FDN has at least one unuploaded collected match record (`match-collection`) or unuploaded crash record (`crash-reporting`), the FDN SHALL transition to the upload state once no PDN PIN lookup is in flight; a lookup ends within `8000 ms`, and its return to the peer-to-peer radio would fail an upload queued behind it.
- On mounting the upload state, the FDN SHALL explicitly transition the wireless subsystem into Wi-Fi HTTP station mode (disabling the peer-to-peer radio and initiating Wi-Fi station association with the configured access point), run a `500 ms` glyph loading sequence followed by displaying `"SYNCING"` with a green chase LED animation, and start a `15,000 ms` fallback timeout timer.
- While Wi-Fi HTTP mode is active, background peer-to-peer send attempts SHALL be rejected without automatically switching the wireless subsystem out of Wi-Fi mode or aborting in-flight HTTP requests.
- Once queued for the upload cycle:
  - Pending collected match records SHALL be uploaded in protobuf batches via `POST /quickdraw-matches` (`match-collection`).
  - Pending local and relayed PDN crash records SHALL be uploaded via `POST /device-logs` (`crash-reporting`).
- The FDN SHALL distinguish a completed upload cycle from an unreachable Wi-Fi/server uplink: when all queued upload requests complete within the window, the FDN SHALL re-enable the peer-to-peer radio and return to the idle state; if the `15,000 ms` fallback timeout expires or the HTTP requests fail before all pending records are confirmed, the FDN SHALL cancel any remaining in-flight HTTP requests, surface an upload failure indication, retain unconfirmed records in non-volatile storage for the next 5-minute cycle, re-enable the peer-to-peer radio, and return to the idle state.

#### Scenario: Five-minute idle timer uploads pending match and crash records and restores peer-to-peer radio
- **WHEN** the `300,000 ms` idle upload timer expires on an FDN holding collected PDN match records and pending crash logs, and the server responds with HTTP 2xx/204
- **THEN** the FDN transitions to Wi-Fi HTTP mode, uploads the pending match batch to `POST /quickdraw-matches` and crash logs to `POST /device-logs`, retires the confirmed records from non-volatile storage, re-enables the peer-to-peer radio, and returns to the idle state

#### Scenario: Upload fallback timeout cancels in-flight HTTP, preserves pending records, and restores peer-to-peer radio
- **WHEN** the upload state is mounted with pending match or crash records and Wi-Fi or the server does not respond within `15,000 ms`
- **THEN** the `15,000 ms` fallback timer expires, any in-flight HTTP requests are cancelled, an upload failure indication is surfaced, unconfirmed records remain persisted for the next 5-minute cycle, the peer-to-peer radio is re-enabled, and the FDN returns to the idle state

#### Scenario: Upload waits out a PIN lookup left running by an unplugged PDN
- **WHEN** the upload timer has expired with collected match records staged and a PIN lookup is still in flight after its PDN was unplugged
- **THEN** the FDN stays in the idle state until the lookup ends, then transitions to the upload state
