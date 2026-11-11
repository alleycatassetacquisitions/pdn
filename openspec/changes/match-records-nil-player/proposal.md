## Why

A match record names both players by server player UUID (`match-collection`). A demo player has a nil player UUID, so a record of its match names no one the server can attribute it to. A record also names a winner, which the server requires and both devices must agree on.

## What Changes

- A demo player with a nil player UUID can initiate and accept standalone matches.
- The self-match guard compares player UUIDs only when the receiver's own UUID is non-nil.
- A match with a nil player UUID on either side is played but not stored for collection.
- The recorded winner is the player with the lower effective draw time, so both devices record the same one. A tie, or a match where the opponent's result never arrived, has no such winner and is played but not stored.

## Capabilities

### Modified Capabilities
- `quickdraw-match`: match initiation for demo players; demo, tied and half-reported matches are not stored.

## Impact

- `src/pdn/game/match-manager.cpp`: initiation guard and match finalization.
