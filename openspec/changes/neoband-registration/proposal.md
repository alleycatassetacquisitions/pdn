## Why

A player registers a PDN today by entering a 4-digit PIN on the PDN's buttons; the PDN sends the PIN to the cabled FDN, which looks it up. The PIN is printed on the player's ID, handed out in order, and crosses the radio in the clear, so any PIN is easy to read or guess. The owner has ruled two things:

- **(B)** A player registers by tapping their Neoband, an NFC band with a factory UID, at an FDN while their PDN is cabled to it. PIN entry goes.
- **(C)** The FDN's HTTPS trusts only a server public key pinned in FDN firmware, not the public certificate authorities.

Neither changes record tags, record keys or the relay of `pdn-record-auth-and-relay`, and neither changes the handover rule or the idle name of `fungible-pdns`.

## What Changes

- **Band reader on the FDN.** Each FDN gets an ISO/IEC 14443-A reader that reads the band's UID only (`4` or `7` bytes), never card data or keys. No hardware driver is written until the owner confirms the part and its pins; the firmware gets a band reader driver interface and native tests use a fake driver.
- **FDN-initiated registration.** When an unprovisioned PDN becomes Connected on a jack, the FDN queues a registration for that jack. One is pending at a time, in arrival order, and the FDN screen names its jack (`"JACK 1"` / `"JACK 2"`). A tap is bound to the PDN on the pending jack at the moment of the tap. An untapped registration times out after `30 s` and goes to the back of the queue; an unplugged PDN leaves it.
- **The PDN asks for nothing.** PIN entry and the PIN request packet are removed; packet type `26` stays reserved; the profile packet loses its `pin` field. The FDN sends the record key on the pending jack and then the profile, or the lookup-failure status, to that jack's PDN.
- **PDN waiting screens.** `"Cable to"` / `"FDN"` when not cabled, `"Tap band"` / `"at FDN"` when cabled, the existing failure screen for `3000 ms` on a failed attempt. The waiting screen keeps firmware distribution (`5000 ms` secondary hold). The demo path moves from PIN `0000` to a `5000 ms` primary hold. The random seed comes from the registered player UUID instead of the PIN.
- **FDN screens.** `"TAP BAND"`, `"LOOKING"` / `"UP"`, `"SEE DESK"` for an unknown or unlinked band, `"SERVER"` / `"UNREACHABLE"`, `"SERVER"` / `"NOT TRUSTED"`, and `"NO READER"`.
- **Staff path.** Staff link a staff card to a named player at the desk, one-shot, and tap it at the FDN. To the firmware it is an ordinary band lookup.
- **Lookup by band.** The FDN asks `GET /device/players/by-band/{uid}`. The PIN stays a server-side identifier only.
- **Pinned server key.** The FDN trusts the server only if its leaf certificate's public key hashes (SHA-256 of the DER SubjectPublicKeyInfo) to a pin set in build configuration next to `BASE_URL`. Host name and validity dates are not checked. `http://`, a missing pin or a malformed pin register nobody. Redirects fail the request. Uploads use the same client. The simulator stays inert. A new server key means reflashing every FDN.
- **`pdn-record-auth-and-relay`** changes its key-delivery rule from "HTTPS with the certificate checked against the CA bundle" to "the pinned key".
- **BREAKING** (wire format): the registration request packet is retired and the profile packet loses its `pin` field. Every PDN and FDN takes the same build.

## Capabilities

### New Capabilities
- `fdn-band-reader`: the FDN reads a band's UID and nothing else, counts a held band as one tap, and reports a reader that does not answer.
- `fdn-server-trust`: the pinned server key, what is and is not checked, redirects, uploads under the same trust, rotation by reflash, and the inert simulator.

### Modified Capabilities
- `player-provisioning`: REMOVED the PIN-entry requirement; ADDED the waiting screens, firmware distribution and the demo path from the waiting screen, the seed from the player UUID, the PDN's acceptance rule, registration after a restart, the FDN registration queue, tap binding, the band lookup, the wire messages and the staff card.
- `device-player-lookup`: the lookup is by band UID.
- `fdn-apps`: provisioning routing is by band tap; ADDED the registration, failure and no-reader screens.
- `player-provider`: ADDED that the provider holds no PIN.

## Impact

- **FDN firmware**: a band reader driver interface and, once the part is confirmed, its driver; a per-jack registration queue in place of the parked PIN request; the band lookup; new registration screens; the pinned-key HTTP client with redirects disabled; the `SERVER_KEY_SHA256` build flag.
- **PDN firmware**: the waiting state replaces PIN entry and FetchUser and receives the key and profile itself; `PdnProvisioningClient` goes; the PIN leaves the player provider; the seed comes from the player UUID.
- **Simulator**: `reboot` lands on the waiting screen; the mock server's route moves from by-pin to by-band; no `tap` command.
- **Server (outside this repository)**: the by-band lookup, desk linking of bands (the existing "Bind Neo ID" story), one-shot staff-card links, the UID kept out of every other response, a question on failed-lookup alerts or limits, and a venue-controlled certificate and key whose public half is supplied before FDNs are flashed (tasks.md section 5).
- **Open PRs**: #441 (PDN key arrival) keeps its key handling and has its PDN waiting state reworked in the B PR; #442 is reworked in place for C; #435 to #440, #443, #444 and the `fungible-pdns` stack are unchanged.
- **Pending requirements superseded at archive**: listed in design.md, "Pending requirements this change supersedes".
