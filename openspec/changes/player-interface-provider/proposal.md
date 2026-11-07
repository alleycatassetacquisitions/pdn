## Why

The server registration model lived in `alleycat-server` as a separate struct from gameplay `Player`, which forced duplicate session state and blocked a clean fetch → Quickdraw handoff.

## What Changes

- Registration fields (server id, name, team, affiliations) live on **`Player`** with dedicated accessors for PlayerFetch.
- **`PlayerProvider`** owns the session **`Player`** and the registration PIN; `main` passes `getPlayer()` to Quickdraw and `PlayerProvider*` to PlayerFetch.
- **`DevicePlayerDecoder`** and **`PlayerApi::getDevicePlayerByPin`** decode into `Player`; a private legacy shim keeps existing Quickdraw call sites working.
- Remove standalone **`PlayerV2`**; keep **`Team`**, **`Affiliation`**, **`PlayerLimits`** in `player-types.hpp`.

Out of scope: Quickdraw boot path switching to PlayerFetch; removing legacy JSON registration.

## Capabilities

- `player-types`: shared value types + registration on `Player`.
- `player-provider`: session owner for `Player` and PIN.

## Impact

- `lib/core` — `player.hpp` / `player.cpp`, `player-provider`, `player-types`.
- `lib/alleycat-server` — decoder and player API target `Player`.
- `src/pdn/apps/player-fetch` — registration via `getPlayer()` new fields.
- `src/pdn/main.cpp` — `PlayerProvider` ownership.
- `test/test_core` — provider, shim, decoder, fetch tests.
