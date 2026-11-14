## Context

The owner made two rulings:

- **(B)** Players register by tapping a Neoband (an NFC band with a factory UID) at an FDN while their PDN is cabled to it. PIN entry goes.
- **(C)** The FDN's HTTPS trusts only a server public key pinned in FDN firmware.

What the code does today, with the firmware of `pdn-record-auth-and-relay` applied:

- **PIN path.** The PDN's PIN entry screen takes 4 digits (`src/pdn/apps/player-fetch/pin-entry-state.cpp:43-67`), seeds `srand` from them (`player-provider.cpp:41-43`, `player.cpp:174-181`), and sends `PlayerProvisionRequestPacket { seqId; pin[5] }` as packet type `26` to the cabled FDN (`fetch-user-state.cpp:20-39`; `player-types.hpp:44-48`). PIN `0000` is the demo path. The FDN checks 4 digits, parks one request if busy (`fdn-provisioning-manager.hpp:290-320`), looks up `GET /device/players/by-pin/{pin}` (`player-api.hpp:15-52`), writes the key on the jack of the requesting MAC, then sends `PlayerProvisionPacket`, which echoes the PIN (`:254-275`). The PDN accepts the profile only if the PIN matches (`fetch-user-state.cpp:126`).
- **FDN.** It has no NFC, RFID or I2C code and no per-jack registration state: one lookup at a time, one parked request, and a jack found from the sender's MAC (`fdn-provisioning-manager.hpp:279-288`). It has one registration screen for both jacks. No FDN state uses a button.
- **FDN pins.** It uses GPIO 9-17, 21 and 38-41; the N8R8 module reserves 26-37 for flash and PSRAM; 0, 3, 45 and 46 are strapping pins, 19 and 20 USB, 43 and 44 UART0. The display's SPI bus has no MISO pin.
- **FDN HTTPS.** `esp_http_client` with `crt_bundle_attach = esp_crt_bundle_attach`, the IDF public-CA bundle (`esp32-s3-http-client-driver.hpp:372-391`). A response counts as server-verified when the configured address and the final URL are `https://` (`:531-539`). Redirects are followed by IDF default and the hops in between are not checked. Nothing sets the clock before a TLS handshake, so validity dates cannot be checked in practice [INFERENCE: IDF default `MBEDTLS_HAVE_TIME_DATE=n`]. Match and crash uploads use the same client. `BASE_URL` comes from `wifi_credentials.ini` as a build flag; the copy in use is `http://`.
- **Simulator.** PDN-only; it boots straight to the hub, and its mock server serves by-pin. Its HTTP driver never reports a verified response.
- **Server** (alleycat-rs, outside this repository). No authentication anywhere. PINs are handed out in order. No band, NFC or UID field exists; the "Bind Neo ID to a Player" story asks for one. The server terminates no TLS itself; the hosting platform presumably does [INFERENCE].
- **Desk flow** (owner's ruling; context the FDN design relies on, not firmware). Every Alleycat player has a NeoBand, issued by Neotropolis (the wider event), not by Alleycat. At the Alleycat desk staff ask whether the player has a band and send them to get one if not; staff take name, affiliation and the hunter/bounty choice, upload it to the server, and scan the band to link it to that player. The player then takes a PDN and registers at an FDN by tapping the band. A lost band means a replacement from the event and a re-link at the desk. No band, no registration; no key, no registration.

## Goals / Non-Goals

**Goals:**

- A player registers by tapping their band at the FDN their PDN is cabled to; nothing is typed on the PDN.
- With two PDNs cabled to one FDN, each tap registers exactly one stated PDN.
- Every player registers with their own band; the FDN has no other registration path.
- The FDN hands a record key only to a server that holds the pinned key.

**Non-Goals:**

- Reading or writing band memory, or any card cryptography.
- A reader driver before the owner confirms the exact reader model and its connection (B14).
- An operator mode, chooser or button action on the FDN.
- Authentication on the server, or a desk login.
- Two posse supporters registered as one player. That question belongs to `fungible-pdns` and is not in scope here.
- Any change to record tags, record keys, the relay, the handover rule or the idle name.

## Decisions

### Placement

One new change, `neoband-registration`, holds B and C. B replaces the registration path and C changes how the FDN trusts the server; neither changes record tags, keys or the relay. Only two parts of `pdn-record-auth-and-relay` move: its key-delivery rule (CA bundle to pin, edited in that change in step 4, E8 and the risks that restate it) and its server request (outside this repository).

**The restart rule and the idle name of `fungible-pdns` still hold.** Registration still happens once per restart, now by band tap. A restart lands on the waiting screen instead of PIN entry ("Registration after a restart").

### B1. Reader: UID only, behind a driver interface

- **Part (owner's ruling):** the D-Logic uFR Zero Online family, OEM module, the family `neoband-app` already drives; the owner linked the "uFR Zero Online Controller OEM module" (https://webshop.d-logic.com/ufr-zero-online-controller-oem-module). Not the PN532.
- **What D-Logic's pages say** (WEB, checked 2026-11; not on the bench):
  - WEB: the series has three models, "μFR Zero Online", "μFR Zero Online Round" and "μFR Zero Online Controller (w/o NFC)"; the Controller lists no supported standards or tags, and its 50x25x8 mm size matches the linked SKU. **The linked part may be the controller without NFC, which cannot read a band by itself.** The reader model is built on an NXP PN7462 plus an ESP32-S3.
  - WEB: interface options USB, TTL UART or RS-232 (an order-time variant); 5 V supply, 180-500 mA.
  - WEB: host-initiated serial frames, no vendor library needed: `55 CMD AA EXTLEN PAR0 PAR1 CHK`, checksum XOR of the bytes plus `0x07`; `GET_CARD_ID_EX` (`0x2C`) returns the card type, UID length (`4`, `7` or `10`) and a 10-byte zero-padded UID; no card answers `NO_CARD` (`0x08`). An optional async mode pushes each UID unasked. Other uFR TTL readers default to 115200 8-N-1.
  - UNKNOWN: see Open Questions, "Reader part".
- **UART plan.** The FDN's three UARTs are taken: UART0 is the console (USB Serial/JTAG is already a secondary console) and UART1 and UART2 drive the jacks (`sdkconfig.esp32-s3_fdn_release:1329-1338`). Moving the FDN console to USB Serial/JTAG only frees UART0 for the reader, on two free GPIOs (for example RX 4, TX 5). That is a build-configuration change for the FDN image only. The reader's 5 V comes from the FDN's 5 V rail.
- UID only; `4`- and `7`-byte UIDs. No card authentication, no keys, no block reads. The FDN holds nothing secret about a band.
- A band held in the field counts as one tap until the reader stops seeing it, so one tap is one lookup.
- Firmware gets the reader as a driver in the existing driver framework: `device/drivers/band-reader.hpp` with a `BandReaderDriverInterface`, `DriverType::BAND_READER` and `BAND_READER_DRIVER_NAME`, listed in `fdnConfig`. `initialize()` is the boot check, `exec()` polls the reader each tick and latches one tap per band, and the provisioning manager reads it through `getPeripheral`. Native tests use a fake driver in the config. **No hardware driver is written until the owner confirms the items in Open Questions, "Reader part" (B14).**

### B2. Tap-to-jack: FDN-initiated queue

- When a PDN whose context reports no applied profile becomes Connected on a jack, the FDN queues a registration for that jack. One is pending at a time, in arrival order; the FDN screen names its jack (B15: `"JACK 1"` is the primary jack, Serial2; `"JACK 2"` the secondary). The pending jack's turn is open only while taps count: not during a lookup or the result hold.
- A tap is bound to the PDN on the pending jack, by its MAC address, at the moment of the tap. Taps with nothing pending, or during a lookup, are ignored.
- A registration is pending only while the registration screen is shown. During symbol match a queued registration waits: nothing is pending, no `30 s` timer runs and taps are ignored, so a tap never takes the radio away from a game in progress. This keeps the old rule that dropped a lookup request while a registered PDN played.
- A pending registration with no tap ends after `30,000 ms`. A PDN that unplugs, or whose context comes to show an applied profile (for example after the demo hold), leaves the queue.
- **Editor's choice N1** (confirmed by the chief). When any registration ends, whatever its outcome, a PDN still cabled whose context shows no applied profile requeues at the back, so the player can tap again without re-cabling. This also covers a profile the FDN counted as delivered (the jack reported the key frame written) that the PDN never applied. The PDN shows its failure screen for `3000 ms` meanwhile.
- **Editor's choice N2** (confirmed by the chief). A UID that is not `4` or `7` bytes is ignored and the registration stays pending.
- **Result hold (NB-4, confirmed by the chief, kept).** After a lookup result the FDN shows its result screen for `3000 ms` and the PDN its result or failure page, discarding pieces. Taps are ignored in that window, and the next turn opens only after it, because a tap then could hand a key to a PDN that throws it away.
- **N6 removed** (owner's ruling 3). The `2000 ms` guard after the turn moved to the other jack protected a player reading the FDN's jack label as the slot rotated. With the turn indicator (B2a) the player reads their own PDN, which stops showing `"Tap band"` when the turn leaves. The residual race, a tap within the radio latency (milliseconds) after a `30 s` timeout rotation, is accepted.
- In the firmware the queue replaces the parked request (`parkRequest`, `PARK_REQUEST_MS`, `parkedPin`) in `FdnProvisioningManager`; `lookupPin` becomes the held UID. `findPdnJack` goes, because the jack is known first, but its check stays: before `sendApplicationFrame` the manager checks `isConnectedPeer(pendingJack, PDN, boundMac)` and ends with `KEY_NOT_ISSUED`, writing no key, if the PDN bound at the tap is no longer the one on the jack.
- Rejected: binding a tap to whichever PDN asked last (the PDN no longer asks), and binding by proximity (no signal ties a band to a jack).

### B2a. Turn indicator

- Owner's ruling 3: only the PDN whose jack holds the open turn shows `"Tap band"`; another cabled unregistered PDN shows a wait screen.
- The FDN adds a turn field to what it already sends a cabled PDN (the FDN connection context sent after the jack HELLO) and re-sends it whenever the turn opens, moves or closes. The implementer picks the exact field after reading the code.
- The turn opens only when taps count (after the result hold), so `"Tap band"` on a PDN means a tap now registers that PDN.
- PDN: `"Tap band"` / `"at FDN"` only while its turn is open; cabled to an FDN otherwise, `"Please"` / `"wait"`; not cabled, `"Cable to"` / `"FDN"`.
- Version skew: an older FDN never sends the field, so a new PDN cabled to it shows the wait screen. Acceptable: PDN and FDN images update together.

### B3, B9. Wire: the PDN sends nothing

- `kPlayerProvisionRequest` (`26`) and `PlayerProvisionRequestPacket` are removed; the number stays reserved so an old PDN's request is dropped, not misread.
- The FDN writes the key on the pending jack, then sends `PlayerProvisionPacket` to the MAC of the PDN on that jack. Its `pin` field is removed. Lookup failure is `status=LOOKUP_FAILED` to that PDN.
- The PDN accepts the profile only from the FDN Connected on its jack, and the key only over that jack, as now. With no request there is no PIN to match against a stale reply; a reply from that FDN is the reply.
- **Editor's choice N3** (confirmed by the chief). The `10,000 ms` wait used to run from FetchUser's mount, right after PIN entry. It now runs from the first piece (key or profile) the PDN accepts. A PDN with nothing yet waits indefinitely; the FDN's `30 s` queue timeout governs the tap.
- The PIN stays a server-side identifier: desk lookups and the `by-pin` routes.

### B4, B10, B11, B12. PDN waiting state

- The waiting state replaces `PinEntryState`. It shows `"Cable to"` / `"FDN"` with no FDN cabled, `"Tap band"` / `"at FDN"` while its jack's turn is open and `"Please"` / `"wait"` otherwise (B2a); it calls `playerProvider->clear()` on mount, as PIN entry did.
- It keeps PIN entry's other jobs: OTA receive eligible while mounted; a `5000 ms` secondary hold enters FirmwareUpdate; FirmwareUpdate returns to it.
- **Editor's choice N4** (confirmed by the chief). It also takes over FetchUser's whole receive job: accepting the key frame and profile, the `10,000 ms` attempt and the `3000 ms` failure page. `FetchUserState`, `PlayerFetchStateId::FETCH_USER` and the PinEntry-to-FetchUser transition pair are deleted, so one state owns the single-slot frame, jack and reply observers. `PdnProvisioningClient` is deleted too: with the request gone it is only a pass-through, and the waiting state holds a `ReliableChannel<PlayerProvisionPacket>` directly, as `FdnProvisioningManager` holds its reply channel. The PIN compare goes.
- Failure: `"Unable to"` / `"Locate Asset"` for `3000 ms`, then the waiting state again.
- B11 demo path: a `5000 ms` primary hold on the waiting screen enters ChooseTeam (kept by the owner, NB-2).
- B12 seed: was seeded from the PIN; now from the registered player UUID when Welcome is entered. The demo path keeps the default seed. `setRegistrationPin`, `getRegistrationPin`, `applyRngSeedFromRegistrationPin` and `Player::applyRngSeedFromDecimalString` are removed.

### B5. FDN screens

| Situation | Top | Bottom |
|---|---|---|
| Pending, waiting for tap | `"TAP BAND"` | jack label |
| Pending, reader not answering | `"NO READER"` | jack label |
| Lookup running | `"LOOKING"` | `"UP"` |
| Profile delivered (`3000 ms`) | `"WELCOME"` | player name |
| Server answered `404` or `400` (`3000 ms`) | `"SEE DESK"` | jack label |
| No answer in `8000 ms`, Wi-Fi or transport failure (`3000 ms`) | `"SERVER"` | `"UNREACHABLE"` |
| Server key not the pin (`3000 ms`) | `"SERVER"` | `"NOT TRUSTED"` |
| No usable key (`3000 ms`, `pdn-record-auth-and-relay`) | `"UNABLE TO"` | `"ISSUE KEY"` |
| Any other failure (`3000 ms`) | `"UNABLE TO"` | `"LOCATE ASSET"` |

- One `"SEE DESK"` screen covers unknown and unlinked bands: the server cannot tell them apart.
- **Editor's choice N5** (confirmed by the chief). `"SEE DESK"` is shown for HTTP `404` and `400`; every other non-2xx status keeps `"UNABLE TO"` / `"LOCATE ASSET"`. `ProvisionLookupResult` widens to `NO_PLAYER`, `SERVER_UNREACHABLE`, `SERVER_NOT_TRUSTED`, `LOOKUP_FAILED`, `KEY_NOT_ISSUED` and `PROFILE_DELIVERED`, and `RegistrationState` keeps one switch over it. `PlayerApi`, which owns the route's status contract, gains a classifier from `WirelessErrorInfo` to that outcome; the manager calls it once in `requestLookup`'s error callback, and its own `8000 ms` deadline gives `SERVER_UNREACHABLE`. The error itself is still forwarded unchanged (`device-player-lookup`). A pin mismatch arrives as `WirelessError::SERVER_NOT_TRUSTED`.
- A reader-less FDN registers nobody: the player goes to another FDN.
- The longest line, `"UNREACHABLE"`, is 11 letters with no wide letter, about `121 px` of the `128 px` display [INFERENCE: from the font widths in `fungible-pdns` design.md, not rendered].

### B6. No staff path

- Owner's ruling 2: every player has a band, so there is no staff registration path at the FDN, no staff cards and no staff links. A player without a working band gets a replacement from the event and a re-link at the desk (Context, "Desk flow").
- Rejected: **an operator card plus an on-FDN player chooser.** The FDN has two buttons and a `128 × 64` display; a chooser over hundreds of players is unusable, and it puts player choice on a device anyone can reach.

### C1-C11. Pinned server key

- **C1. Key pin, not certificate pin.** The pin is SHA-256 over the DER SubjectPublicKeyInfo of the leaf certificate (64 hex). Rejected: **pinning the certificate** — every renewal, even with the same key, would need a reflash. A key pin survives renewal with the same key.
- **C2. Config.** `[wifi] SERVER_KEY_SHA256` in `wifi_credentials.ini` (not committed), passed as `-DSERVER_KEY_SHA256` next to `-DBASE_URL`. `wifi_credentials.ini.example` gains the key. The pin reaches the driver through `WifiConfig`, beside `baseUrl`, so one object carries the server's address and its key.
- **C3. Mechanism.** `Esp32S3HttpClient::initializeHttpClient()` sets `crt_bundle_attach` to our own function instead of `esp_crt_bundle_attach`. esp-tls sets `MBEDTLS_SSL_VERIFY_REQUIRED` and calls it (ESP-IDF 5.5.3). It installs a dummy CA chain and an `mbedtls_ssl_conf_verify` callback, the way `esp_crt_bundle` does. Depth 0: hash the SPKI and compare with the pin; a match clears the flags, a mismatch **sets** `MBEDTLS_X509_BADCERT_NOT_TRUSTED` and logs the presented hash. Depth > 0: flags cleared, because trust is decided at depth 0. Keeping the flags on a mismatch would fail open: mbedtls marks "not trusted" only on the topmost certificate of the chain, so a leaf signed by an intermediate the server itself sends reaches depth 0 with no flags, and the final verdict is the OR of what each call returns. Public CAs are trusted for nothing. `#include <esp_crt_bundle.h>` goes. The comparison and the per-depth flag rule are pure functions, unit-tested natively (C8).
- **C4. Host name and validity dates are not checked.** The venue may lack DNS or NTP and the clock is set only after a handshake; the pin is the identity. SNI is still sent. Rejected: **checking the host name** — it adds nothing once the key is pinned, and it breaks a venue server reached by IP address or a local name.
- **C5.** An `http://` address, or one with no scheme (which `buildRequestUrl` turns into `http://`), registers nobody: no key is accepted. A missing or malformed pin trusts no server, so it also registers nobody. Uploads over an `http://` address still run unprotected, as today.
- **C6.** A mismatch fails the handshake; the driver reports it at once as `WirelessError::SERVER_NOT_TRUSTED` and does not retry it (a retry cannot succeed, and the retry's backoff could push the lookup past its `8000 ms` and show `"UNREACHABLE"`). The presented hash is logged in the driver, for lookups and uploads alike, so staff can compare it with the pin. For a lookup the FDN shows `"SERVER"` / `"NOT TRUSTED"` for `3000 ms` and sends the lookup-failure reply to the PDN, as E1 of `pdn-record-auth-and-relay`.
- **C7.** Match and crash uploads use the same client and the same pin; an untrusted server is an upload failure and records stay.
- **C11. Redirects disabled** (`disable_auto_redirect = true`). Every hop would otherwise need the pin; a redirect is a failed request.
- **C8.** The simulator stays inert for HTTPS (E5 of `pdn-record-auth-and-relay`).
- **C10.** The server side needs a venue-controlled certificate and key, the public half supplied before flashing. A key the hosting platform rotates on its own cannot be pinned [INFERENCE: the platform terminates TLS today].
- `isResponseServerVerified` keeps its name; its meaning becomes "the response came over HTTPS from a server presenting the pinned key". With redirects refused and the pin enforced in the handshake, every response that arrives over an `https://` `BASE_URL` came from the pinned key, so the value is a fixed property of the build: computed once when the client is configured, true exactly when `BASE_URL` is `https://` and the pin is well-formed. The per-response reset and the transport-type check go. Its doc comments in the driver, `http-client-interface.hpp`, `wireless-manager.hpp` and `player-api.hpp` follow.

### Pending requirements this change supersedes

Other open changes MODIFY or ADD these requirements, so this change does not MODIFY them again. Its ADDED requirements state the new behaviour; once both changes archive, the PIN wording of each pending requirement below is replaced as shown (tasks.md 7.2).

| Pending requirement | Change | Superseded by |
|---|---|---|
| `player-provisioning` "FDN server lookup and protobuf profile decoding": the PIN validation bullet | `pdn-record-auth-and-relay` | "FDN band lookup" |
| `player-provisioning` "Cabled FDN-to-PDN profile delivery and wire identity": "after it has sent that FDN its PIN", the `10,000 ms` wait for a reply, "the FDN the PDN sent its PIN to", the demo profile of PIN `0000`, and the scenarios naming a PIN | `pdn-record-auth-and-relay` | "PDN accepts a registration only from its jack's FDN", "Demo path from the waiting screen", "Registration wire messages" |
| `fdn-apps` "Registration screen for a cabled unprovisioned PDN": `"REGISTER"` / `"ENTER CODE"`, "waiting for a lookup request", the drop-request rule, the failure screen, and the scenarios naming a PIN | `pdn-record-auth-and-relay` | "Registration screens for band taps", "Registration failure screens", "Registration screen with no reader", `player-provisioning` "FDN registration queue" |
| `record-authentication` "Record key": the scenario "a player enters the demo PIN `0000`" | `pdn-record-auth-and-relay` | "Demo path from the waiting screen" |
| `player-provisioning` "Handing a PDN to another player": "PIN entry" | `fungible-pdns` | "Registration after a restart" |
| `player-provider` "Clear registration session": "the registration PIN", "the PIN entry screen" | `fungible-pdns` | `player-provider` "No registration PIN on the PDN" |

The record-key rule in "FDN server lookup and protobuf profile decoding" is not superseded: `pdn-record-auth-and-relay` itself now says "pinned key" (`neoband-registration`).

### Found, not changed

- `device-player-lookup` keeps the scenario title "Unknown PIN" because a MODIFIED requirement keeps its base scenario titles; its steps are generic. The capability's Purpose still says by-pin. Both are fixed at archive (tasks.md 7.3).
- The `fdn-apps` scenario "Unprovisioned PDN connected to FDN is provisioned on credential scan" keeps its title; a band UID is an identifier, not a credential.

## Risks / Trade-offs

Accepted by the owner:

- **B7. Band copy at contact range.** A UID is sent in the clear and can be copied or emulated by anyone who gets a reader within a few centimetres of the band. The risk is one player at a time.
- **The unauthenticated server.** The server authenticates nothing, so desk linking is only as safe as the venue network: anything on it can link any band to any player, or read and change links. The owner states that only FDNs and operators are on that network once the Wi-Fi password is changed (`pdn-record-auth-and-relay`, answer 1a); we have not verified it.
- **B8. Guessing band IDs** (owner's ruling 5: neither alert nor rate limit). With $N$ linked bands, a random 4-byte UID hits one about once per $2^{32}/N$ lookups; with 2,000 bands, about 6 hours at 100 lookups a second from the venue network. A 7-byte UID needs at most about $2^{48}/N$, fewer if batch serials share leading bytes. The risk rests on the venue network being closed; nothing is asked of the server. This supersedes the PIN note E9 of `pdn-record-auth-and-relay`.
- **One FDN key, every FDN** (owner's ruling 6, KT-1). One pinned key, no spare: a leaked server private key lets anyone with the venue network impersonate the server to every FDN until all are reflashed with a new pin.
- **Taps during a timeout rotation.** A tap within the radio latency after a `30 s` rotation can register the newly pending PDN before its screen changes (B2, N6 removed).

Other trade-offs:

- **A stuck queue.** A PDN cabled and never tapped cycles through the queue every `30 s` and delays any PDN behind it on the other jack by up to `30 s` per turn.
- **Upgrading in pairs.** A PDN on older firmware sends a PIN request nobody answers; an FDN on older firmware waits for a request a new PDN never sends. Neither registers with the other.

## Migration Plan

1. **Owner confirms** the reader model and connection (Open Questions, "Reader part"). Firmware for C and for the non-driver parts of B, the turn indicator included, proceeds meanwhile, against the band reader driver interface and a fake driver. Moving the FDN console to USB Serial/JTAG only, to free UART0 for the reader, is part of the FDN image build.
2. **PR survival.** #435 to #440, #443 and #444 are unchanged. #441 keeps its PDN key arrival; its PDN waiting state is reworked in the B PR. #442 is reworked in place for C (new commits), and the merge cascades up through #443, #444, #445 and the `fungible-pdns` stack, which is otherwise unaffected.
3. **Server team** builds the by-band lookup and desk linking, and supplies the venue certificate's public key (tasks.md section 5). Until the by-band route exists no PDN can register with this firmware.
4. **Build FDN images** with `SERVER_KEY_SHA256` set to the venue key's SPKI hash and an `https://` `BASE_URL`.
5. **One roll-out.** PDN and FDN images must update together because the wire changes (the request packet is retired and the profile packet loses its `pin`). This can be the roll-out of `pdn-record-auth-and-relay` and is under its gate: not flashed to event devices before the server verifies tags.
6. **Key rotation means reflashing every FDN** with an image carrying the new pin. Renewing the certificate for the same key needs no reflash. Plan the venue key's lifetime around the event.
7. **Rollback** means reflashing every PDN and FDN to the previous build together, and the server keeping the by-pin route.

## Open Questions

Answered by the owner:

- **Reader part:** the D-Logic uFR Zero Online family, OEM module, not the PN532 (B1). Still to confirm before the driver (tasks.md 1.3), each UNKNOWN on D-Logic's pages: the exact model (the reader "μFR Zero Online" or the linked controller without NFC, which needs an external reader); the interface variant (TTL UART expected); the connector pinout; the UART logic level (3.3 V or 5 V); the default baud (115200 8-N-1 assumed); whether async UID output goes to the TTL UART and persists; the reset pin and its polarity; whether the FDN's 5 V rail can supply 180-500 mA; agreement to move the FDN console to USB Serial/JTAG only and give UART0 to the reader.
- **Staff path:** none. Every player has a band; desk flow in Context (ruling 2).
- **Pending jack on the PDN:** built now, the turn indicator (B2a, ruling 3). N6 removed; the result hold kept.
- **Demo path:** keep the `5000 ms` primary hold (NB-2).
- **Band-ID guessing:** neither alert nor rate limit; accepted risk resting on the closed venue network; no server ask (ruling 5, B8).
- **One pinned server key:** accepted; a leak means reflashing every FDN; no spare key (ruling 6).

Confirmed by the chief: N1 to N5 as built, and the result hold (NB-4). N6 is removed (B2).

Not asked here: whether two posse supporters registered as the same player both count. That belongs to `fungible-pdns`.
