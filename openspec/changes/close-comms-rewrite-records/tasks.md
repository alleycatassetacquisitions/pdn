## 1. PR 4: openspec: close-comms-rewrite-records; release flashing keeps NVS
- [x] 1.1 Write this change with the roll-out gate
- [x] 1.2 Drop `--clear-nvs` from the release envs; `--erase` hints warn it wipes records

## 2. PR 5: pdn: test-only player identity; cabled simulator devices connect
- [ ] 2.1 MAC-derived test UUID in `esp32-s3_pdn_test` and the simulator; native HELLO; CI test build

## 3. PR 6: match: record with player UUIDs in its own ring; PDN stops uploading matches
- [ ] 3.1 112 B record in `match_ring`; full ring refuses; no-store rules; `matches/*` untouched

## 4. PR 7: fdn: store relayed records; ack after storing; beacon from the tick
- [ ] 4.1 `fdn_records` intake, dedupe, full refusal; beacon with `senderDeviceType` in every app

## 5. PR 8: pdn: relay crash and match records to an FDN over ESP-NOW
- [ ] 5.1 Crash and match lanes on the beacon trigger; retire on a matching ack

## 6. PR 9: fdn: upload relayed matches and crash records to today's endpoints
- [ ] 6.1 Uploader at boot and every 300 s; retire and keep rules

## 7. PR 10: pdn: no Wi-Fi or HTTP; registration screen first; archive spent changes
- [ ] 7.1 Remove PDN Wi-Fi, HTTP, PIN entry and fetch; sync and archive this change
