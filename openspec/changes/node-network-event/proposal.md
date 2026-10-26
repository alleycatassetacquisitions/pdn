## Why

Metagame 2026 needs a **net-new firmware experience**: PIN fetch via the alleycat-rs device API, jack-linked PDN↔PDN duels scored by **reaction sync** (not fastest draw), and match upload via **`POST /quickdraw-matches`**. Legacy Quickdraw + `MatchManager` + JSON registration remain for normal firmware until after the event. Implementation should **not** depend on the old `metagame` branch (`NodeMatchManager`, JSON edge metadata).

## What Changes

- **Depends on [`node-network-duel-manager`](../node-network-duel-manager/proposal.md)** for `MatchManagerV2` and its session, wireless, and storage units.
- Add **`NodeNetwork`** app: embeds **`PlayerFetchApp`** (like Quickdraw embeds registration), owns **`PlayerProvider`**, wires wireless callbacks to **`MatchManagerV2` only**.
- Add **`node-network-states/`**: fresh port of minimal duel + upload UX from Quickdraw (not metagame cherry-pick).
- Event build: **`main`** mounts **NodeNetwork** only; Quickdraw may remain in the link graph but is **not** loaded into the app config.

## Non-Goals

- Mounting PlayerFetch from Quickdraw or replacing `PlayerRegistrationApp` on the default Quickdraw boot path.
- Shootout, chain duel, symbol match, or FDN jack flows in NodeNetwork.
- Device-side `delta_ms` or custom JSON match fields — front end derives sync from uploaded draw times.
- Cherry-picking or merging the historical `metagame` branch implementation.

## Capabilities

### New Capabilities

- `node-network-app`: NodeNetwork state machine shell, PlayerFetch embedding, MatchManagerV2 wiring.
- `node-network-event-flow`: Jack roles, sync success/fail UX, success-only upload.

### Dependencies

- [`node-network-duel-manager`](../node-network-duel-manager/proposal.md): `duel-session`, `node-network-wireless`, `match-storage`, `match-manager-v2`.

### Modified Capabilities

- `player-fetch-flow`: When embedded in NodeNetwork, awaken completion **may** hand off to NodeNetwork gameplay; Quickdraw still does not reference PlayerFetch.

## Impact

- `src/pdn/game/node-network.hpp`, `node-network.cpp`, `node-network-states/`
- `src/pdn/main.cpp`, `platformio.ini` (event env)
- Legacy `MatchManager`, `Quickdraw`, `player-registration/` unchanged for default env
