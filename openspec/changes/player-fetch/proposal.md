## Why

Stage 1 added protobuf player lookup (`DevicePlayer`, `PlayerApi::getDevicePlayerByPin`), but firmware still uses `PlayerRegistrationApp` with legacy JSON fetch and extra fetch-side behavior. We need a **standalone** `PlayerFetchApp` that exercises the new flow without touching Quickdraw or Idle — gameplay hookup waits until `Player` and `DevicePlayer` are unified (team vs hunter, affiliations vs legacy allegiance enum).

## What Changes

- Add **`PlayerFetchApp`** under `src/pdn/apps/player-fetch/` as a **self-contained state machine** (compiled and testable; **not** mounted from `quickdraw.cpp` in this change).
- **Pin entry** — copy of `player-registration-state.cpp` as `PinEntryState`, without `MatchManager` or transmit-breath. PIN `0000` → **ChooseTeam**; any other PIN → **FetchUser**.
- **Fetch user** — loading glyphs + `getDevicePlayerByPin` only. Success → **Welcome**; failure/timeout → confirm-offline **page 0**, then pin entry after 3 s.
- **Choose team** — Hunter/Bounty demo UI; confirm → **demo stub state** (no welcome, no awaken, no handoff to gameplay).
- **Welcome** — `Welcome` + name; timer → **AwakenSequence** inside the app.
- **Awaken** — copy/move into `player-fetch/` for the server success path only; `readyForGameplay()` may be implemented for API parity but **nothing in Quickdraw consumes it yet**.
- Session holds fetched **`DevicePlayer`** (app-owned or on `Player` where needed for display); **no `isHunter`** on PlayerFetch paths.

Unchanged in this change: `quickdraw.cpp`, `PlayerRegistrationApp`, legacy `Player` shape for gameplay, demo gameplay, Idle transition.

## Capabilities

### New Capabilities

- `player-fetch-flow`: Standalone pin → fetch → welcome → awaken app; demo stub; OTA from pin entry.
- `player-device-profile`: PlayerFetch session stores canonical `DevicePlayer` from fetch (not full gameplay `Player` migration).

### Modified Capabilities

None.

## Impact

- `src/pdn/apps/player-fetch/` — new files only (+ build entries so the app links).
- No `quickdraw.cpp` or CLI default boot path changes unless a dedicated test entry is added later.
- Follow-up: unify `Player` with `DevicePlayer`, wire PlayerFetch → Idle, delete `player-registration/`.
