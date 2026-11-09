## Context

PDNs operate exclusively over serial jacks and the peer-to-peer radio and never connect to Wi-Fi. FDNs act as the base station with Wi-Fi connectivity to the Alleycat server. The player enters a PIN on the PDN, which forwards it to the FDN Connected on a serial jack; the FDN resolves the player's profile from the server and delivers it to the cabled PDN over a reliable peer-to-peer provisioning channel. Offline registration on the PDN is removed; PIN `0000` remains as a demo path (`pdn-pin-entry-fdn-registration`).

## Goals / Non-Goals

**Goals:**

- Provisioning app on the PDN waits for FDN-delivered player registration without using Wi-Fi on the PDN, accepting provisioning frames only from the verified MAC address of an FDN currently Connected on a serial jack.
- Ensure unprovisioned PDNs start with a nil player UUID (advertising the `0xFFFF` / nil UUID sentinel rather than a random boot UUID).
- Remove offline registration and the PDN Wi-Fi lookup.
- On receiving a resolved player profile from the Connected FDN: display `Welcome` (`5000 ms`) in `PlayerFetchApp`, then transition across the app boundary to `HubApp` (`AwakenSequence` -> `Idle`).
- On lookup failure from the Connected FDN: display `Unable to` / `Locate Asset` for `3000 ms` and return to PIN entry.
- Preserve the `5000 ms` secondary-button hold on the PIN entry screen to seed peer-to-peer firmware distribution.

**Non-Goals:**

- Offline registration on the PDN.
- Direct Wi-Fi HTTP requests from the PDN.

## Decisions

### State graph

```
PinEntry --(PIN entered)--> FetchUser --(PIN sent to Connected FDN)
FetchUser --(profile received from Connected FDN)--> Welcome (5s) --> HubApp (AwakenSequence --> Idle)
FetchUser --(lookup failed or timed out)--> [Unable to Locate Asset] --3s--> PinEntry
PinEntry --(PIN 0000)--> ChooseTeam --> HubApp
PinEntry --(5s secondary button hold)--> FirmwareUpdate --> PinEntry
```

### Session storage

When the Connected FDN delivers a resolved player profile, the PDN applies the profile to the session `Player` owned by `PlayerProvider`, populating the 16-byte player UUID, 16-byte team UUID, display name (up to 31 ASCII characters in a 32-byte buffer), up to 5 affiliations, standing role (`Hunter` or `Bounty`, defaulting to `Bounty` when unassigned), and allegiance (defaulting to `Alleycat` when unrecognized or absent).
