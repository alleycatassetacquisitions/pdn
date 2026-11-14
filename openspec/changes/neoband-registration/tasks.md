## 1. Decisions

- [x] 1.1 Owner ruled B: registration by band tap at an FDN while the PDN is cabled to it; PIN entry goes
- [x] 1.2 Owner ruled C: the FDN's HTTPS trusts only a server public key pinned in FDN firmware
- [ ] 1.3 Owner confirms the reader before its driver (design.md B1, Open Questions "Reader part"): the exact model (the "μFR Zero Online" reader, or the linked controller without NFC plus an external reader); the TTL UART interface variant; the connector pinout; the UART logic level (3.3 V or 5 V); the default baud (115200 8-N-1 assumed); whether async UID output goes to the TTL UART and persists; the reset pin and its polarity; the FDN 5 V rail's headroom for 180-500 mA; moving the FDN console to USB Serial/JTAG only so UART0 serves the reader
- [x] 1.4 Owner answered: no staff path, every player has a band linked at the desk (ruling 2); build the turn indicator now (ruling 3); keep the demo-path primary hold (NB-2); no alert or rate limit for band-ID guessing (ruling 5); one pinned server key, no spare (ruling 6)
- [x] 1.5 Chief confirmed N1 to N5 and the result hold (NB-4); N6 removed by the turn indicator (design.md B2)

## 2. (a) FDN HTTP client: pinned key (C) — #442 reworked in place

- [x] 2.1 Build config: `[wifi] SERVER_KEY_SHA256` in `wifi_credentials.ini`, passed as `-DSERVER_KEY_SHA256` next to `-DBASE_URL` in the FDN envs of `platformio.ini`; add it to `wifi_credentials.ini.example`; a missing or malformed value builds and trusts no server; the pin reaches the driver through `WifiConfig` beside `baseUrl` (#442)
- [ ] 2.2 Pure functions: parse the 64-hex pin, hash a DER SubjectPublicKeyInfo with SHA-256, compare in constant time; the per-depth flag rule of 2.3; native unit tests for match, mismatch, missing pin, 63 characters, non-hex characters, and the flag rule (a depth-0 mismatch with no incoming flags still sets a flag)
- [x] 2.3 Own `crt_bundle_attach` in `Esp32S3HttpClient::initializeHttpClient()`: dummy CA chain plus `mbedtls_ssl_conf_verify` callback; depth 0 compares the SPKI hash (a match clears the flags; a mismatch sets `MBEDTLS_X509_BADCERT_NOT_TRUSTED` and `LOG_E`s the presented hash, for lookups and uploads alike); depth > 0 clears the flags; drop `esp_crt_bundle_attach` and `#include <esp_crt_bundle.h>` (#442)
- [x] 2.4 Host name and validity dates are not checked; SNI is still sent (#442)
- [x] 2.5 `disable_auto_redirect = true`; a redirect answer is a failed request (#442)
- [x] 2.6 `isResponseServerVerified` means "HTTPS and the pinned key presented": true for a response only when `BASE_URL` is `https://`, the pin is well-formed and the response came over TLS (which, with redirects refused, means the pinned key was presented); an `http://` or scheme-less address yields no accepted key; update the doc comments in the driver, `http-client-interface.hpp`, `wireless-manager.hpp` and `player-api.hpp` (#442)
- [x] 2.7 A pin mismatch is reported at once as `WirelessError::SERVER_NOT_TRUSTED` and is never retried; for a lookup it becomes `ProvisionLookupResult::SERVER_NOT_TRUSTED` (3.7): lookup-failure reply to the PDN, `"SERVER"` / `"NOT TRUSTED"` for `3000 ms` (#442)
- [x] 2.8 Match and crash uploads keep using the same client; a mismatch or redirect is an upload failure and the records stay (#442)
- [x] 2.9 Native HTTP driver stays inert: no response is ever server-verified (#442)

## 3. (b) FDN side of B

- [ ] 3.1 Band reader driver interface in the driver framework: `device/drivers/band-reader.hpp`, `BandReaderDriverInterface`, `DriverType::BAND_READER`, `BAND_READER_DRIVER_NAME`, an entry in `fdnConfig`; `initialize()` is the boot check, `exec()` polls for a UID (`4` or `7` bytes) or "no reader" and latches a band held in the field as one tap until it leaves; the provisioning manager reads it through `getPeripheral`; a fake driver for native tests
- [ ] 3.2 **Blocked on 1.3.** uFR reader driver over UART0 behind the band reader interface: raw frames (`GET_CARD_ID_EX` `0x2C`, or async UID if confirmed), UID only, no block reads; UIDs other than `4` or `7` bytes ignored (N2)
- [ ] 3.2a FDN image build config: console on USB Serial/JTAG only, UART0 free for the reader (after 1.3)
- [x] 3.3 Per-jack registration queue in `FdnProvisioningManager` replacing the parked request: queue on Connected with `registered=false`; one pending in arrival order and only while the registration screen is mounted (during symbol match nothing is pending, no timer runs, taps are ignored) and shows no lookup result (for `3000 ms` after a lookup ends nothing is pending, so a tap reaches neither a PDN on its failure page nor a jack the screen has not named); `30,000 ms` timeout; when any registration ends, whatever its outcome, a PDN still Connected with `registered=false` requeues at the back (N1); unplug or a context showing `registered=true` leaves the queue
- [x] 3.4 Tap binding: a tap while pending and no lookup is running is bound to the MAC of the PDN on the pending jack at that moment; taps with nothing pending, during a lookup, or during the `3000 ms` result hold are ignored; UIDs of other lengths are ignored (N2); before `sendApplicationFrame`, `isConnectedPeer(pendingJack, PDN, boundMac)` must hold, else `KEY_NOT_ISSUED` with no key written
- [x] 3.5 `PlayerApi::getDevicePlayerByBand`: `GET /device/players/by-band/{uid}`, upper-case hex without separators; remove `getDevicePlayerByPin`, `onProvisionRequest`, `parkRequest`, `lookupPin`, `parkedPin`; the UID is held only for its lookup
- [x] 3.6 Delivery: key frame on the pending jack, then `PlayerProvisionPacket` without `pin` to that jack's PDN; `LOOKUP_FAILED` to that PDN on failure; drop packet type `26` and leave the number reserved
- [x] 3.7 Widen `ProvisionLookupResult` to `NO_PLAYER`, `SERVER_UNREACHABLE`, `SERVER_NOT_TRUSTED`, `LOOKUP_FAILED`, `KEY_NOT_ISSUED`, `PROFILE_DELIVERED`, classified once in `requestLookup`'s error callback by a `PlayerApi` classifier over `WirelessErrorInfo` (`404` or `400` gives `NO_PLAYER` (N5), a transport failure `SERVER_UNREACHABLE`, `WirelessError::SERVER_NOT_TRUSTED` gives `SERVER_NOT_TRUSTED`) and by the manager's own `8000 ms` deadline (`SERVER_UNREACHABLE`). Screens, one switch over it: `"TAP BAND"` / `"NO READER"` over `"JACK 1"` (primary, Serial2) or `"JACK 2"` (secondary); `"LOOKING"` / `"UP"`; `"SEE DESK"` / jack label; `"SERVER"` / `"UNREACHABLE"`; `"SERVER"` / `"NOT TRUSTED"`; existing `"WELCOME"`, `"ISSUE KEY"` and `"LOCATE ASSET"`
- [x] 3.4a Turn indicator on the FDN (design.md B2a): add the turn field to what the FDN already sends a cabled PDN (the connection context after the jack HELLO); set it open only for the pending jack while taps count (not during a lookup or the result hold); re-send it to the affected jacks whenever the turn opens, moves or closes
- [x] 3.8 Update `fdn-provisioning-tests.hpp` (replace the request, parking and malformed-PIN cases with queue, binding and tap cases, a tap during the result hold, and turn-field cases: only the pending jack is told its turn is open, the turn moves on timeout and requeue, and reopens only after the result hold), `device-player-codec-tests.hpp` by-pin cases and `fdn-record-uploader-tests.hpp:680-684`

## 4. (c) PDN side of B — #441's waiting state reworked in the B PR

- [x] 4.1 Waiting state replaces `PinEntryState`: `"Cable to"` / `"FDN"` with no FDN cabled, `"Tap band"` / `"at FDN"` only while the cabled FDN's turn field says its jack's turn is open, `"Please"` / `"wait"` otherwise (also when an older FDN sends no field); redraw when the field changes; `playerProvider->clear()` on mount; OTA receive eligible while mounted; `5000 ms` secondary hold enters FirmwareUpdate, which returns to the waiting state
- [x] 4.2 Demo path: `5000 ms` primary hold enters ChooseTeam; remove `DEMO_PIN`
- [x] 4.3 Receive without a request (N3, N4), in the waiting state: accept the key only over the jack of the Connected FDN and the profile or failure only from that FDN; the first accepted piece starts a `10,000 ms` attempt; failure, timeout or that FDN leaving shows `"Unable to"` / `"Locate Asset"` for `3000 ms`, then the waiting screen; delete `FetchUserState`, `PlayerFetchStateId::FETCH_USER`, the PinEntry-to-FetchUser transitions, `PdnProvisioningClient` and `pdn-provisioning-client.hpp` (the waiting state holds a `ReliableChannel<PlayerProvisionPacket>` directly), `requestProvisionFromCabledFdn` and the PIN compare
- [x] 4.4 Remove the PIN from `PlayerProvider` (`setRegistrationPin`, `getRegistrationPin`, the buffer) and `PlayerProvisionRequestPacket`; remove `pin` from `PlayerProvisionPacket` and update its size assertion
- [x] 4.5 Seed `srand` from the player UUID on entering Welcome; the demo path keeps the default seed; remove `applyRngSeedFromRegistrationPin` and `Player::applyRngSeedFromDecimalString`
- [x] 4.6 Simulator: drop the device PIN from `DeviceFactory`; `reboot` lands on the waiting state; mock server route by-pin to by-band; no `tap` command; update `CLI_README.md`
- [x] 4.7 Update `player-fetch-tests.hpp` (PIN entry and request cases become waiting-state, demo-hold and receive-without-request cases, plus wait screen vs `"Tap band"` as the turn field opens, moves and closes, and the wait screen with no field), `IdleStateTests` `pairingCode*`, `cliDeliverCabledProvision` and the CLI HTTP server tests

## 5. (d) Server request (outside this repository)

- [ ] 5.1 Device lookup keyed by band UID, e.g. `GET /device/players/by-band/{uid}`, returning the same protobuf as by-pin (with the record key of `pdn-record-auth-and-relay` item 2); `404` for an unknown or unlinked band, `400` for a malformed UID
- [ ] 5.2 Desk linking: store the band UID on player create or update (the existing "Bind Neo ID to a Player" story)
- [ ] 5.3 The band UID appears in no device response and not in `GET /players` or any other player response
- [ ] 5.4 A venue-controlled certificate and key for the server; the public half (or its SPKI SHA-256) supplied before FDNs are flashed

## 6. Verification on devices

- [ ] 6.1 Two PDNs cabled to one FDN: only the PDN on the pending jack shows `"Tap band"`, the other `"Please"` / `"wait"`; each tap registers the PDN that showed `"Tap band"`, in arrival order
- [ ] 6.2 An untapped registration moves to the back after `30 s`; an unplugged PDN leaves the queue
- [ ] 6.3 An unlinked band shows `"SEE DESK"`; the PDN shows its failure screen and then `"Tap band"`; a second tap registers it once linked
- [ ] 6.4 The uFR reader on UART0 reads `4`- and `7`-byte bands as one tap each, with the FDN console on USB Serial/JTAG (after 3.2)
- [ ] 6.5 Against a server with the pinned key: registration succeeds; against one with another key from a public CA, or a leaf with another key chained to an intermediate the server sends: `"SERVER"` / `"NOT TRUSTED"` at once and the presented hash in the log; a redirecting server fails the lookup
- [ ] 6.6 An FDN built with an `http://` address or no pin registers nobody
- [ ] 6.7 An FDN with the reader unplugged shows `"NO READER"` and still runs symbol match and uploads

## 7. Roll-out and archive

- [ ] 7.1 One roll-out: the same build on every PDN and FDN, because the wire changes; under the gate of `pdn-record-auth-and-relay`
- [ ] 7.2 At archive, after `pdn-record-auth-and-relay` and `fungible-pdns`, apply the supersessions in design.md, "Pending requirements this change supersedes", to the archived requirement text
- [ ] 7.3 At archive, rename the `device-player-lookup` scenario "Unknown PIN" and update that capability's Purpose and the `player-provisioning` and `fdn-apps` Purpose paragraphs to the band lookup
- [ ] 7.4 Rotating the server key: reflash every FDN with the new pin
