## 1. Core types and interface

- [ ] 1.1 Add `lib/core/include/game/player-types.hpp` with `Team`, `Affiliation`, and `PlayerInterface` (`getTeam`, not a teams vector)
- [ ] 1.2 Add `lib/core/include/game/player-provider.hpp` with `PlayerProvider` (hasPlayer, getPlayer with default + LOG_E when empty, setPlayer, clear)
- [ ] 1.3 Add concrete session class (e.g. extend `PlayerFetchSession`) implementing `PlayerProvider` with `DevicePlayer` storage

## 2. DevicePlayer conformance

- [ ] 2.1 Make `DevicePlayer` inherit `PlayerInterface` and implement getters (`getTeam` from teamId/teamName per design)
- [ ] 2.2 Align affiliation member type with `Affiliation` (remove duplicate struct if feasible) without proto/nanopb regen
- [ ] 2.3 Verify `DevicePlayerDecoder` and existing device-player tests still pass

## 3. Tests

- [ ] 3.1 Native tests: `DevicePlayer` satisfies interface scenarios (team, affiliations, active)
- [ ] 3.2 Native tests: provider set/clear/overwrite via mock `PlayerInterface`; default `getPlayer` when empty (LOG_E, no crash)
- [ ] 3.3 Run `pio test -e native -f test_core`

## 4. PlayerFetch wiring (optional same PR or immediate follow-up)

- [ ] 4.1 Point PlayerFetch states at `PlayerProvider*` instead of session-specific API where applicable
- [ ] 4.2 Fetch callback uses `setPlayer` (interface copy; metadata not retained in session)
- [ ] 4.3 Awaken uses default Alleycat art (remove affiliation scan)

## 5. Documentation

- [ ] 5.1 Deprecation comment on gameplay `Player` in `game/player.hpp`
- [ ] 5.2 Cross-link from `openspec/changes/player-fetch/design.md` session section to this change (manual note or follow-up edit when archiving)
