## 1. PDN

- [x] 1.1 Remove the PDN HTTP client driver and WiFi credentials; `WirelessManager` fails HTTP calls closed without one
- [x] 1.2 Store standalone matches in a 32-record NVS ring with a sequence counter and a collected high-water mark
- [x] 1.3 Send uncollected records to the cabled FDN one at a time; advance the mark only on its acknowledgement
- [x] 1.4 `FetchUserState` sends the entered PIN to the cabled FDN and applies its provisioning reply, including the wall clock
- [x] 1.5 Keep PIN entry, the demo path, and the OTA hold on PIN entry

## 2. FDN

- [x] 2.1 64-record match spool: stage, deduplicate by match UUID, acknowledge, withhold when full
- [x] 2.2 Upload the spool from the idle upload state; retire on a decodable response, keep on error, stop short of and then drop unreadable records
- [x] 2.3 Provisioning manager: validate the PIN, look it up with a 15 s deadline, reply to the requesting PDN if still Connected

## 3. Shared

- [x] 3.1 Fixed-width match record (uint32 draw times), effective draw time floored at 0 ms
- [x] 3.2 `PlayerLimits` back to 37-byte names and 3 affiliations, matching `device_api.options`
- [x] 3.3 Remote device coordinator query for the peer of a device type on a Connected jack

## 4. Verification

- [x] 4.1 Native tests for collection, spool, provisioning, clock, and a device without HTTP
- [x] 4.2 Build `esp32-s3_pdn_release`, `esp32-s3_fdn_release`, `esp32-s3_pdn_debug`, `native_cli`
- [ ] 4.3 Hardware: PIN registration through a cabled FDN, match collection, and an FDN upload
