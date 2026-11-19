## Why

A PDN today takes any PIN and plays as a nil or test identity, the first upload of a match decides its result, and a radio attacker can forge a standalone match result. Players need to register a PDN with their NeoBand, every match record needs a tag only the player's key can make, and a PDN must be safe to hand to another player. Registration (#485), record authentication (#486) and PDN swaps (#487) ship as one delivery.

## What Changes

- FDN uses normal TLS: the server is verified against ESP-IDF's public CA bundle with host name checks; the address must be `https://` and redirects are refused; SNTP sets the clock before the first TLS connection, and requests wait for it. No pinned key.
- Posse: a supporter counts only as a registered player other than the challenger's, once per player per round; its player UUID travels in the posse confirmation.
- The PDN idle screens start with the registered player's name.
- One opaque application frame on the serial jack, and HMAC-SHA-256.
- Standalone match frames are signed with a 16-byte per-match secret sent over the cable; early signed results are held; there is no reaction-time floor.
- Match records carry a 16-byte tag made with the player's derived key, held in RAM only; the relay frame, acknowledgement and FDN store carry it.
- The FDN reads a band UID through a reader interface (no hardware driver yet), looks the player up by band over verified HTTPS, hands the key over the jack, then the profile. No key, no registration.
- The PDN's first screen becomes the band waiting screen ("Tap band" only while its jack holds the FDN's turn); it applies a profile only with its record key, from the FDN on its jack, within 10 s, and then tells that FDN it registered. A 5 s hold enters the demo.
- Practice matches (#498, merged from #543): only a match between two real, distinct player IDs is stored, tagged when a key is held; idle opens on `UNREGISTERED` in place of the name for a nil player ID.
- The PDN's match ring moves to its own flash range at the end of flash, registered at boot so deployed partition tables need no USB flash; only the test build's table lists it. Without the range the ring stays in the default storage.

## Non-goals

A band-reader hardware driver (#485 Q1). Server totals on the PDN (#487 Q1–Q4). Each side's posse boost and supporter-side support records in a new record layout, and ties under tags (#486 Q4, Q5). Upload of the raw record, tag or originating MAC (#486 Q2, Q3). The FDN's record store partition (#463) is a separate PR.

## Roll-out gate

Test-only. Not flashed to event devices until #485, #486 and #487 have landed together. Event devices stay on tag `event-firmware-2026-11b` (`main` @ `2f7d3957`). This replaces the "server verifies tags" gate of the earlier record-authentication design. Lifting the gate also needs a reader driver, server keys and the FDN's storage partition (#463).

## Capabilities

New: `fdn-server-trust`, `fdn-band-reader`, `record-authentication`. Modified: `chain-roster`, `pdn-session-routing`, `quickdraw-match`, `match-collection`, `record-relay`, `player-provisioning`, `player-provider`, `fdn-apps`, `device-player-lookup`, `player-types`, `wireless-transport`, `http-date-clock-sync`.
