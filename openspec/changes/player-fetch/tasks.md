## 1. PlayerFetch app (standalone)

- [x] 1.1 Add `src/pdn/apps/player-fetch/` — `PlayerFetchApp`, state IDs, headers; register in build (no Quickdraw reference)
- [x] 1.2 `PinEntryState` from `player-registration-state.cpp` minus MatchManager/breath; `0000` → demo mode menu vs fetch
- [x] 1.3 `FetchUserState` — glyphs, `getDevicePlayerByPin`, session `DevicePlayer` on success; fail/timeout → page 0 → pin entry (3 s)
- [x] 1.4 `ChooseTeamState` from choose-role UI; confirm → welcome on demo path
- [x] 1.5 `DemoModeMenuState` — PLAY → choose team; TEST → device test operator
- [x] 1.6 `WelcomeState` — `Welcome` + name → awaken
- [x] 1.7 `AwakenSequence` under `player-fetch/`; `readyForGameplay()` after awaken
- [x] 1.8 Firmware OTA transitions on pin entry (copy from legacy app)

## 2. Tests

- [x] 2.1 Core/state tests: fetch queues protobuf path; session retains DevicePlayer; `0000` → demo mode menu, no HTTP
- [x] 2.2 Fetch failure shows page 0 then pin entry (timer behavior)
- [x] 2.3 Demo menu TEST reaches device test; choose team confirm reaches welcome
- [ ] 2.4 Build native + `esp32-s3_pdn_release` with new app linked, Quickdraw unchanged

## 3. Follow-up (explicitly not this change)

- [ ] Unify `Player` with `DevicePlayer`; remove gameplay dependence on `isHunter`
- [ ] Wire PlayerFetch in Quickdraw → Idle; remove `player-registration/`
- [ ] Complete demo mode path (gameplay handoff wired via welcome)
