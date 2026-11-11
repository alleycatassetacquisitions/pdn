## Why

PDNs do not use Wi-Fi or offline registration; player registration is performed by connecting a PDN to an FDN on a serial jack, where the FDN resolves the player's registration against the Alleycat server and delivers the resulting profile to the cabled PDN (`player-provisioning`).

## What Changes

- Replace PDN Wi-Fi PIN lookup and offline registration with FDN-driven player provisioning in the PDN provisioning app (`player-provisioning`).
- Ensure an unprovisioned PDN boots with a nil player UUID (never generating a random boot UUID before provisioning) and waits on the provisioning screen (where it also accepts or seeds peer-to-peer firmware distribution) until a Connected FDN delivers a resolved player profile.
- Upon receiving a resolved player profile from the verified MAC of a Connected FDN, the PDN stores the profile on the session, displays the welcome screen (`Welcome` + player name) for `5000 ms`, and transitions into the hub app (`AwakenSequence` -> `Idle`).
- On lookup failure from the Connected FDN, the PDN displays `Unable to` / `Locate Asset` for `3000 ms` and returns to the provisioning screen without offering offline registration.

## Capabilities

### New Capabilities

None (`player-provisioning` and `pdn-session-routing` in `openspec/specs/` define the canonical domain requirements).

### Modified Capabilities

- `player-provisioning`: Implement the PDN provisioning app and cabled FDN profile reception (`WaitingForFdn` -> `Welcome` -> `HubApp`).
- `pdn-session-routing`: Transition from the provisioning app's `Welcome` state into `HubApp` (`AwakenSequence` -> `Idle`).

## Impact

- `src/pdn/main.cpp` — Do not assign a random boot `userID` before FDN provisioning so unprovisioned PDNs advertise the nil UUID / `0xFFFF` sentinel.
- `src/pdn/apps/player-fetch/` — FDN-driven provisioning flow without PDN Wi-Fi or offline registration states.
- `src/fdn/` — FDN resolves player identity against the server and delivers the profile to the Connected PDN.
