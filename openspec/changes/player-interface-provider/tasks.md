## 1. Core types

- [x] 1.1 `player-types.hpp`: `PlayerLimits`, `Team`, `Affiliation` (registration payload on `Player`)
- [x] 1.2 `Player` registration fields, accessors, legacy shim, `applyRegistrationProfile` / `clearRegistrationProfile`
- [x] 1.3 `PlayerProvider` owns session `Player`; PIN + clear; `getPlayer()` returns non-null pointer

## 2. alleycat-server

- [x] 2.1 `DevicePlayerDecoder` and `PlayerApi::getDevicePlayerByPin` decode into `Player`
- [x] 2.2 `static_assert`s tie `PlayerLimits` to `device_api.options`

## 3. Tests

- [x] 3.1 Decoder and provider tests target `Player` registration accessors
- [x] 3.2 Registration shim tests (allegiance, hunter team name, UUID user id)
- [x] 3.3 PlayerFetch tests use `getServerPlayerName()`

## 4. PlayerFetch wiring

- [x] 4.1 States take `PlayerProvider*`; registration reads via `getPlayer()` new fields only
- [x] 4.2 Fetch callback decodes into owned `Player`
- [x] 4.3 `main` constructs `PlayerProvider`; Quickdraw uses `playerProvider->getPlayer()`

## 5. Documentation

- [x] 5.1 OpenSpec player-provider and player-types deltas updated for unified model
