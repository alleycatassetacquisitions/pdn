## 1. Core types

- [x] 1.1 Add `lib/core/include/game/player-types.hpp` with `PlayerLimits`, `Team`, `Affiliation`, and `PlayerV2` (moved from alleycat-server `DevicePlayer`; `Team team` replaces `teamId` / `teamName`)
- [x] 1.2 Add concrete `PlayerProvider` (`player-provider.hpp` / `.cpp`): hasPlayer, getPlayer with empty default + LOG_E, setPlayer, clear

## 2. alleycat-server

- [x] 2.1 Delete `device-player.hpp`; decoder and `PlayerApi` use `PlayerV2`; `static_assert`s tie `PlayerLimits` to `device_api.options`
- [ ] 2.2 Verify `DevicePlayerDecoder` and existing device-player tests still pass

## 3. Tests

- [x] 3.1 Rename decoder tests to `PlayerV2` / `Team` / `Affiliation`
- [x] 3.2 Native tests: provider empty, default `getPlayer`, store, overwrite, clear
- [ ] 3.3 Run `pio test -e native -f test_core`

## 4. PlayerFetch wiring

- [x] 4.1 Remove `PlayerFetchSession`; `PlayerFetchApp` owns `PlayerProvider`; states take `PlayerProvider*`
- [x] 4.2 Fetch callback uses `setPlayer`
- [x] 4.3 Awaken uses default Alleycat art (remove affiliation scan)

## 5. Documentation

- [x] 5.1 Deprecation comment on gameplay `Player` in `game/player.hpp`
- [x] 5.2 Cross-link from `openspec/changes/player-fetch/design.md` session section to this change
