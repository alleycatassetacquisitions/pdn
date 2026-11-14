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

## Goals / Non-Goals

**Goals:**

- A player registers by tapping their band at the FDN their PDN is cabled to; nothing is typed on the PDN.
- With two PDNs cabled to one FDN, each tap registers exactly one stated PDN.
- Staff can register a player who has no working band without any new FDN control.
- The FDN hands a record key only to a server that holds the pinned key.

**Non-Goals:**

- Reading or writing band memory, or any card cryptography.
- A reader driver before the part and pins are confirmed.
- An operator mode, chooser or button action on the FDN.
- Authentication on the server, or a desk login (owner's item, Open Questions).
- Two posse supporters registered as one player. That question belongs to `fungible-pdns` and is not in scope here.
- Any change to record tags, record keys, the relay, the handover rule or the idle name.

## Decisions

### Placement

One new change, `neoband-registration`, holds B and C. B replaces the registration path and C changes how the FDN trusts the server; neither changes record tags, keys or the relay. Only two parts of `pdn-record-auth-and-relay` move: its key-delivery rule (CA bundle to pin, edited in that change in step 4, E8 and the risks that restate it) and its server request (outside this repository).

**The restart rule and the idle name of `fungible-pdns` still hold.** Registration still happens once per restart, now by band tap. A restart lands on the waiting screen instead of PIN entry ("Registration after a restart").

### B1. Reader: UID only, behind a driver interface

- Recommended part: NXP PN532 over I2C. The ESP32-S3 has two I2C controllers and the display uses SPI, so the reader shares no bus. An SPI reader would need a MISO pin and its own CS.
- UID only, from ISO 14443-A anticollision; `4`- and `7`-byte UIDs. No MIFARE Crypto1, no keys, no block reads. The FDN holds nothing secret about a band.
- A band held in the field counts as one tap until the reader stops seeing it, so one tap is one lookup.
- Firmware gets the reader as a driver in the existing driver framework: `device/drivers/band-reader.hpp` with a `BandReaderDriverInterface`, `DriverType::BAND_READER` and `BAND_READER_DRIVER_NAME`, listed in `fdnConfig`. `initialize()` is the boot check, `exec()` polls the reader each tick and latches one tap per band, and the provisioning manager reads it through `getPeripheral`. Native tests use a fake driver in the config. **No hardware driver is written until the owner confirms the part and the pins (B14).**
- Pins (B14): candidates for SDA and SCL are two of GPIO 1, 2, 4-8, 18, 42, 47, 48. The owner or the board design picks.
- Rejected: reading a stored ID from band memory (needs keys or a writable band; the owner said factory UID).

### B2. Tap-to-jack: FDN-initiated queue

- When a PDN whose context reports no applied profile becomes Connected on a jack, the FDN queues a registration for that jack. One is pending at a time, in arrival order; the FDN screen names its jack (B15: `"JACK 1"` is the primary jack, Serial2; `"JACK 2"` the secondary).
- A tap is bound to the PDN on the pending jack, by its MAC address, at the moment of the tap. Taps with nothing pending, or during a lookup, are ignored.
- A registration is pending only while the registration screen is shown. During symbol match a queued registration waits: nothing is pending, no `30 s` timer runs and taps are ignored, so a tap never takes the radio away from a game in progress. This keeps the old rule that dropped a lookup request while a registered PDN played.
- A pending registration with no tap ends after `30,000 ms`. A PDN that unplugs, or whose context comes to show an applied profile (for example after the demo hold), leaves the queue.
- **Editor's choice N1.** When any registration ends, whatever its outcome, a PDN still cabled whose context shows no applied profile requeues at the back, so the player can tap again without re-cabling. This also covers a profile the FDN counted as delivered (the jack reported the key frame written) that the PDN never applied. The PDN shows its failure screen for `3000 ms` meanwhile.
- **Editor's choice N2.** A UID that is not `4` or `7` bytes is ignored and the registration stays pending.
- **Editor's choice N6.** A tap within `2000 ms` after the pending registration passes from one jack to the other is ignored, so a player who read `"TAP BAND"` for one jack does not register the PDN on the other one as the slot rotates.
- In the firmware the queue replaces the parked request (`parkRequest`, `PARK_REQUEST_MS`, `parkedPin`) in `FdnProvisioningManager`; `lookupPin` becomes the held UID. `findPdnJack` goes, because the jack is known first, but its check stays: before `sendApplicationFrame` the manager checks `isConnectedPeer(pendingJack, PDN, boundMac)` and ends with `KEY_NOT_ISSUED`, writing no key, if the PDN bound at the tap is no longer the one on the jack.
- Rejected: binding a tap to whichever PDN asked last (the PDN no longer asks), and binding by proximity (no signal ties a band to a jack).

### B3, B9. Wire: the PDN sends nothing

- `kPlayerProvisionRequest` (`26`) and `PlayerProvisionRequestPacket` are removed; the number stays reserved so an old PDN's request is dropped, not misread.
- The FDN writes the key on the pending jack, then sends `PlayerProvisionPacket` to the MAC of the PDN on that jack. Its `pin` field is removed. Lookup failure is `status=LOOKUP_FAILED` to that PDN.
- The PDN accepts the profile only from the FDN Connected on its jack, and the key only over that jack, as now. With no request there is no PIN to match against a stale reply; a reply from that FDN is the reply.
- **Editor's choice N3.** The `10,000 ms` wait used to run from FetchUser's mount, right after PIN entry. It now runs from the first piece (key or profile) the PDN accepts. A PDN with nothing yet waits on `"Tap band"` indefinitely; the FDN's `30 s` queue timeout governs the tap.
- The PIN stays a server-side identifier: desk lookups and the `by-pin` routes.

### B4, B10, B11, B12. PDN waiting state

- The waiting state replaces `PinEntryState`. It shows `"Cable to"` / `"FDN"` with no FDN cabled and `"Tap band"` / `"at FDN"` with one; it calls `playerProvider->clear()` on mount, as PIN entry did.
- It keeps PIN entry's other jobs: OTA receive eligible while mounted; a `5000 ms` secondary hold enters FirmwareUpdate; FirmwareUpdate returns to it.
- **Editor's choice N4.** It also takes over FetchUser's whole receive job: accepting the key frame and profile, the `10,000 ms` attempt and the `3000 ms` failure page. `FetchUserState`, `PlayerFetchStateId::FETCH_USER` and the PinEntry-to-FetchUser transition pair are deleted, so one state owns the single-slot frame, jack and reply observers. `PdnProvisioningClient` is deleted too: with the request gone it is only a pass-through, and the waiting state holds a `ReliableChannel<PlayerProvisionPacket>` directly, as `FdnProvisioningManager` holds its reply channel. The PIN compare goes.
- Failure: `"Unable to"` / `"Locate Asset"` for `3000 ms`, then the waiting state again.
- B11 demo path: a `5000 ms` primary hold on the waiting screen enters ChooseTeam. **The owner may veto this** (Open Questions).
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
- **Editor's choice N5.** `"SEE DESK"` is shown for HTTP `404` and `400`; every other non-2xx status keeps `"UNABLE TO"` / `"LOCATE ASSET"`. `ProvisionLookupResult` widens to `NO_PLAYER`, `SERVER_UNREACHABLE`, `SERVER_NOT_TRUSTED`, `LOOKUP_FAILED`, `KEY_NOT_ISSUED` and `PROFILE_DELIVERED`, and `RegistrationState` keeps one switch over it. `PlayerApi`, which owns the route's status contract, gains a classifier from `WirelessErrorInfo` to that outcome; the manager calls it once in `requestLookup`'s error callback, and its own `8000 ms` deadline gives `SERVER_UNREACHABLE`. The error itself is still forwarded unchanged (`device-player-lookup`). A pin mismatch arrives as `WirelessError::SERVER_NOT_TRUSTED`.
- A reader-less FDN registers nobody, staff cards included, because a staff card is tapped at the same reader: staff take the player to another FDN. This amends B5, which left such an FDN to the staff path.
- The longest line, `"UNREACHABLE"`, is 11 letters with no wide letter, about `121 px` of the `128 px` display [INFERENCE: from the font widths in `fungible-pdns` design.md, not rendered].

### B6. Staff path: staff cards

- At the desk, staff link a staff-held card to the named player, one-shot: the server consumes the link on the first successful lookup. Staff tap it at the FDN like any band.
- The server spends the link on its own `200`, whatever the FDN does next. A reply lost and retried by the HTTP driver (the retry gets `404`), an answer after the FDN's `8000 ms` deadline, or a failure after the answer (no usable key, the PDN gone, the key frame not written) leaves the link spent and nobody registered; staff re-link at the desk. Whether to ask for a short-lived reusable link instead is an owner question (Open Questions).
- Firmware is unchanged by it: it is a band lookup. The FDN cannot tell an operator from a player and does not need to; the operator names the player at the desk.
- Needs from the owner: a desk login (the server has no authentication) and staff cards.
- Rejected: **an operator card plus an on-FDN player chooser.** The FDN has two buttons and a `128 × 64` display; a chooser over hundreds of players is unusable, and it puts player choice on a device anyone can reach.
- Rejected: **the desk pushes a registration to an FDN.** It needs a server-to-FDN channel that does not exist; the FDN only polls when it holds Wi-Fi.

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

For the owner to weigh:

- **B8. Brute force by UID.** With $N$ linked bands, a random 4-byte UID hits one about once per $2^{32}/N$ lookups. With 2,000 bands that is about 2.1 million lookups, about 6 hours at 100 lookups a second against the server from the venue network. A 7-byte UID (NXP: a 1-byte maker code and 6 bytes of unique serial number, not random [INFERENCE: NXP AN10927, not checked against the bands]) needs at most about $2^{48}/N$, some 140 billion lookups for 2,000 bands, decades at that rate. That is an upper bound: serials from one batch can share leading bytes, so an attacker who knows one band's UID may search far fewer. At an FDN a UID emulator could present arbitrary UIDs, but each lookup costs a Wi-Fi excursion of seconds [INFERENCE], so the server route is the fast one. This supersedes the `10,000`-lookup PIN note (E9 of `pdn-record-auth-and-relay`): a cabled device can no longer name the player whose key it wants. Whether to ask the server for a failed-lookup alert or a rate limit is the owner's call (Open Questions).
- **The PDN does not know whether its jack is pending.** It shows `"Tap band"` whenever an FDN is cabled, and only the FDN screen names the pending jack. A player who taps while the other jack is pending (someone queued ahead, or cabled to the other jack on purpose) registers that PDN with their key. N6 closes only the race of the slot rotating as they tap. Whether the FDN should tell the PDN that its jack is pending is an owner question (Open Questions).
- **A stuck queue.** A PDN cabled and never tapped cycles through the queue every `30 s` and delays any PDN behind it on the other jack by up to `30 s` per turn.
- **One FDN key, every FDN.** A leaked server private key lets anyone with the venue network impersonate the server to every FDN until all are reflashed with a new pin. Rotation is a reflash of every FDN.
- **Upgrading in pairs.** A PDN on older firmware sends a PIN request nobody answers; an FDN on older firmware waits for a request a new PDN never sends. Neither registers with the other.

## Migration Plan

1. **Owner confirms** the reader part and pins (B1, B14), the demo-path hold (B11), and the staff cards and desk login (B6). Firmware for C and for the non-driver parts of B proceeds meanwhile, against the band reader driver interface and a fake driver.
2. **PR survival.** #435 to #440, #443 and #444 are unchanged. #441 keeps its PDN key arrival; its PDN waiting state is reworked in the B PR. #442 is reworked in place for C (new commits), and the merge cascades up through #443, #444, #445 and the `fungible-pdns` stack, which is otherwise unaffected.
3. **Server team** builds the by-band lookup, desk linking, staff-card links, and supplies the venue certificate's public key (tasks.md section 5). Until the by-band route exists no PDN can register with this firmware.
4. **Build FDN images** with `SERVER_KEY_SHA256` set to the venue key's SPKI hash and an `https://` `BASE_URL`.
5. **One roll-out.** PDN and FDN images must update together because the wire changes (the request packet is retired and the profile packet loses its `pin`). This can be the roll-out of `pdn-record-auth-and-relay` and is under its gate: not flashed to event devices before the server verifies tags.
6. **Key rotation means reflashing every FDN** with an image carrying the new pin. Renewing the certificate for the same key needs no reflash. Plan the venue key's lifetime around the event.
7. **Rollback** means reflashing every PDN and FDN to the previous build together, and the server keeping the by-pin route.

## Open Questions

For the owner:

- **Reader part and pins.** PN532 over I2C is recommended; which two GPIOs (from 1, 2, 4-8, 18, 42, 47, 48), and is it on the board design?
- **Staff cards and desk login.** The staff path needs staff-held cards and a desk login on a server that has no authentication. Who provides each?
- **Staff-card link lifetime.** One-shot links are spent by the server's `200` even when the FDN then registers nobody (B6). Keep one-shot and re-link at the desk, or ask the server for a link that stays valid for a few minutes and is reusable within them? Recommended: the short-lived reusable link.
- **Pending jack on the PDN.** Should the FDN tell a PDN that its jack is pending, so only that PDN shows `"Tap band"` (Risks)? It needs a field on the wire and a PDN "wait" screen. Recommended: yes, in a later change.
- **Brute force.** Ask the server for a failed-lookup alert, a rate limit, both, or neither (B8)?
- **Demo path.** Keep the `5000 ms` primary hold on the waiting screen, or veto it (B11)?
- **Editor's choices N1 to N6**, above.

Not asked here: whether two posse supporters registered as the same player both count. That is open in `fungible-pdns` and is not in scope for this change.
