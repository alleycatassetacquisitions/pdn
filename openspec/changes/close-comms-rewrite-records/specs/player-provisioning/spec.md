## MODIFIED Requirements

### Requirement: Unprovisioned PDN state and prohibition of offline or PDN Wi-Fi registration

An unprovisioned PDN SHALL wait for registration without using Wi-Fi or offering offline registration:

- On boot, a PDN SHALL initialize with no registered profile and a nil 16-byte player UUID (never generating a random player ID at boot that could masquerade as a provisioned profile). After the boot splash hold, an unprovisioned PDN SHALL remain in the provisioning app until registered or a role is chosen.
- A PDN SHALL NOT enable Wi-Fi station mode, SHALL NOT issue HTTP player-lookup requests directly, and a release build SHALL NOT offer offline registration or PIN entry. It MAY offer role selection; the player keeps the nil player UUID, and its matches are stored with it (`match-collection`).
- While waiting on the provisioning screen, the PDN SHALL remain eligible to receive peer-to-peer firmware distribution images and SHALL start seeding its running firmware image when the secondary button is released and held for `5000 ms` (`firmware-distribution`).

#### Scenario: Unprovisioned PDN waits for FDN without Wi-Fi or offline fallback
- **WHEN** a release-build PDN boots with no provisioned profile and no FDN connected
- **THEN** the PDN has a nil player UUID, stays in the provisioning app with the peer-to-peer radio enabled, and never offers offline registration or PIN entry

#### Scenario: Holding secondary button for 5 seconds on provisioning screen starts firmware seeding
- **WHEN** a user releases and holds the secondary button for `5000 ms` on the PDN provisioning screen
- **THEN** the PDN enters the firmware distribution seeding state (`firmware-distribution`)

## ADDED Requirements

### Requirement: Test-only registration

A PDN built as a test build, and a simulated PDN, SHALL register itself with a test player UUID derived from its MAC address, which never equals a server-issued player UUID, so its matches are stored and relayed. A release build SHALL contain no such path. Test builds SHALL NOT be flashed to event devices.

#### Scenario: Two test PDNs record a match naming both
- **WHEN** two test-build PDNs with different MAC addresses finish a match
- **THEN** each stores a match record naming both test player UUIDs
