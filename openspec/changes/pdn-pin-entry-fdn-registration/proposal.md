## Why

The FDN has no credential reader yet, so a player cannot present a credential at the FDN as `player-provisioning` expects. Until one exists, the PDN keeps its registration screen: the player enters their PIN on the PDN's buttons and the PDN hands it to the cabled FDN, which does the lookup. The registration screen also keeps a demo path for showing the game without a server account. While an unprovisioned PDN is cabled, the FDN shows the player where registration stands.

## What Changes

- The PDN registration screen takes a 4-digit PIN on its buttons and sends it to the FDN Connected on one of its jacks. The FDN looks the PIN up and replies with the profile or a lookup failure.
- PIN `0000` is a demo path: the player picks Hunter or Bounty and enters the hub. A demo player has no server player UUID, so its matches are not stored.
- While an unprovisioned PDN is Connected, the FDN shows a registration screen: waiting for the code, looking it up, then the player's name or the lookup failure.

## Capabilities

### Modified Capabilities
- `player-provisioning`: PIN entry on the PDN, forwarded to the cabled FDN, and the demo PIN.
- `quickdraw-match`: a demo player with a nil player UUID can play; its matches are not stored.
- `fdn-apps`: the FDN registration screen for a cabled unprovisioned PDN.

## Impact

- `src/pdn/apps/player-fetch/`: PIN entry, fetch user, demo role picker, provisioning client.
- `lib/alleycat-server/`: FDN provisioning manager.
- `src/fdn/apps/idle/`: registration state and routing.
