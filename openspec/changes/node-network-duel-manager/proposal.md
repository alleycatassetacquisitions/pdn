## Why

Legacy `MatchManager` is a monolith: ESP-NOW protocol, button callbacks, hunter/bounty scoring, chain boost, shootout exceptions, player stat mutation, UI snapshots, and JSON persistence in one class. The event firmware (NodeNetwork) needs sync-window scoring, UUID-based identity, and `QuickdrawMatchRecord` upload, with none of the Quickdraw tournament coupling.

`QuickdrawWirelessManager` and `QuickdrawCommand` are also overloaded (4-character player ids, `isHunter` doubling as a role flag, shootout/chain semantics). The event path gets its own radio layer instead of adapting them.

This change delivers a small, testable duel stack **before** NodeNetwork states exist, the same boundary `PlayerFetchApp` had.

## What Changes

- **`DuelSession`**: in-memory duel facts (match id, link side, peer identity, draw times, completion flags, agreed timestamp). Projects itself into a `QuickdrawMatchRecord` once complete.
- **`NodeNetworkWirelessManager` + `NodeNetworkPacket`**: new ESP-NOW packet and command set carrying full UUIDs. Modeled on Quickdraw's handshake/draw flow, not wire-compatible with it.
- **`MatchStorage`**: persists the pending `QuickdrawMatchRecord`(s) awaiting upload. Usually zero or one.
- **`MatchManagerV2`**: thin facade the Node states call. Owns the session, wireless, and storage; applies the sync window; tracks the failed-attempt limit.

## Non-Goals

- Changing `MatchManager`, `QuickdrawWirelessManager`, or any Quickdraw state.
- A pluggable outcome evaluator or duel builder. The sync window is applied inside `MatchManagerV2` for now; the seam is deferred until it has been designed.
- Shootout, chain duel, symbol match, boost, or button-masher penalty.
- HTTP upload. `NodeUploadMatches` (NodeNetwork change) reads `MatchStorage` and calls `MatchesApi`.
- Uploading `delta_ms`, sync flags, or failed attempts.

## Capabilities

### New Capabilities

- `duel-session`: in-memory duel state and upload-record projection
- `node-network-wireless`: NodeNetwork duel packet, command set, send/receive, peer gating
- `match-storage`: persistence of pending upload records
- `match-manager-v2`: facade, sync decision, failed-attempt limit

### Modified Capabilities

None.

## Impact

- New code under `src/pdn/game/node-network/` and `src/pdn/wireless/` for the packet and wireless manager.
- A new `PktType` value in `lib/core/include/device/drivers/peer-comms-types.hpp`.
- New native tests under `test/test_core/`.
- No edits to legacy match, Quickdraw, or registration code.

## Dependencies

Merged on main: `Player` registration fields (`getServerPlayerId`, `getRegistrationTeam`), `PlayerProvider`, `UUIDBytes`, `QuickdrawMatchRecord`, `QuickdrawMatchCodec`, `PlatformClock::unixMilliseconds`.
