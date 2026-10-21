## Context

Legacy registration remains the production boot path via Quickdraw. PlayerFetch is a greenfield app to validate protobuf registration UX and state wiring before the **`Player` / `DevicePlayer` model gap** (team vs hunter, affiliations vs enum) is closed for Idle and duel code.

## Goals / Non-Goals

**Goals:**

- Standalone `PlayerFetchApp`: **PinEntry**, **FetchUser**, **ChooseTeam**, **DemoStub**, **Welcome**, **AwakenSequence**, **FirmwareUpdate**.
- Fetch via `getDevicePlayerByPin`; failure page 0 + 3 s → PinEntry.
- Server path: Welcome → Awaken inside app.
- Demo path: ChooseTeam → **DemoStub** (placeholder UI or idle loop back to pin; must not crash).
- Keep `player-registration/` and Quickdraw **untouched**.

**Non-Goals:**

- Mounting PlayerFetch from Quickdraw or transitioning to Idle.
- Fully working demo mode or post-awaken gameplay.
- Migrating global `Player` for all gameplay fields in this change (minimal storage for welcome name is OK).
- Deleting legacy registration or JSON APIs.

## Decisions

### Standalone app boundary

`PlayerFetchApp` is a normal `StateMachine` subclass with its own app id, built with the firmware target, but **not referenced** from `Quickdraw::populateStateMap`. Optional later: CLI test harness or perf entry to mount it in isolation.

*Why:* Avoids half-migrated Player types breaking Idle, duel, and sleep paths.

### State graph

```
PinEntry --(pin != 0000)--> FetchUser --success--> Welcome --> Awaken --> [terminal / hold in app]
PinEntry --(pin == 0000)--> ChooseTeam --confirm--> DemoStub
FetchUser --fail|timeout--> [page 0] --3s--> PinEntry
PinEntry --OTA hold--> FirmwareUpdate --> PinEntry
```

**DemoStub:** minimal state (e.g. “Demo” text or log + return to PinEntry on button, or no-op loop). No Welcome, no Awaken, no `readyForGameplay()` assertion required for demo.

### DevicePlayer storage

Fetch success copies `DevicePlayer` into **session storage** owned by PlayerFetch (e.g. member on app or fetch state) and/or minimal fields on `Player` for welcome display (`name`). Full “`Player` canonical DevicePlayer” lands when gameplay unifies — spec capability `player-device-profile` applies to the **fetch session**, not global gameplay migration yet.

> Superseded by `openspec/changes/player-interface-provider`: `DevicePlayer` moved to core as `PlayerV2`, `PlayerFetchApp` owns a `PlayerProvider` (replacing the session type), states hold `PlayerProvider*`, and Awaken uses default Alleycat art.

### Awaken

Relocate/copy `AwakenSequence` under `player-fetch/` for Welcome → Awaken only. Quickdraw keeps its existing Awaken instance and registration app until cutover.

### readyForGameplay()

May exist on `PlayerFetchApp` (true after Awaken completes) for parity with `PlayerRegistrationApp`, but **no parent state machine wires a transition** in this change.

## Risks / Trade-offs

- [Dead code until wired] → Acceptable; build inclusion proves compile; unit/state tests target PlayerFetch directly.
- [Duplicate Awaken implementation] → Copy or share file without moving Quickdraw’s link until cutover.
- [Demo stub too minimal] → Explicit; expand when Player type unifies.

## Migration Plan

1. Ship standalone PlayerFetch + tests.
2. Unify `Player` with `DevicePlayer`; migrate gameplay off `isHunter`.
3. Wire Quickdraw to PlayerFetch; remove registration app + duplicate Awaken from Quickdraw.
4. Flesh out demo path if still needed.

## Open Questions

None blocking standalone delivery.
