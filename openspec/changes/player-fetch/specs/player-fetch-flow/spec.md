## ADDED Requirements

### Requirement: PDN registration is received from a Connected FDN without PDN Wi-Fi or offline registration

A PDN SHALL NOT connect to Wi-Fi, issue HTTP player-lookup requests, or offer offline registration, PIN entry, or manual role selection. Instead, the PDN SHALL boot with a nil player UUID and remain on its provisioning screen until an FDN Connected on a physical serial jack resolves the player's registration from the server and delivers the player profile to the PDN (`player-provisioning`).

#### Scenario: Unprovisioned PDN waits for Connected FDN registration
- **WHEN** an unprovisioned PDN boots into the provisioning app with no FDN connected
- **THEN** the PDN advertises the nil UUID / `0xFFFF` unregistered sentinel, remains on the provisioning screen, does not enable Wi-Fi, and does not offer offline registration or manual role selection

### Requirement: Cabled FDN-resolved player profile triggers welcome and transitions to hub app

When a PDN on the provisioning screen receives a valid resolved player profile from the verified MAC address of an FDN Connected on a serial jack, the PDN SHALL store the profile on the session, transition to the welcome screen displaying `"Welcome"` and the player's display name for `5000 ms`, and then transition to the hub app (`pdn-session-routing`), which runs its 20-step (`100 ms` per step) awaken sequence before mounting the hub idle state.

#### Scenario: Connected FDN delivers player profile to PDN
- **WHEN** a Connected FDN resolves a player's registration from the server and delivers the profile to the PDN
- **THEN** the PDN stores the profile on the session, displays `"Welcome"` and the player's name for `5000 ms`, and transitions to the hub app (`Awaken` -> `Hub Idle`)

### Requirement: Failed FDN lookup returns PDN to provisioning screen without offline fallback

If the Connected FDN reports that a player lookup failed or timed out, the PDN SHALL display `"Unable to"` / `"Locate Asset"` for `3000 ms` and then return to the provisioning screen without offering an offline registration fallback.

#### Scenario: FDN lookup failure shows error for 3000 ms and returns to provisioning screen
- **WHEN** a player lookup at the Connected FDN fails or times out
- **THEN** the PDN displays `"Unable to"` / `"Locate Asset"` for `3000 ms` and returns to the provisioning screen without offering offline registration
