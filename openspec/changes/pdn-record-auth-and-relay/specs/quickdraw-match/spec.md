## ADDED Requirements

### Requirement: Same-player match refusal

A PDN whose own player UUID is non-nil SHALL NOT play a standalone match against an opponent whose player UUID equals its own, whether it initiated the match or was invited to it. An initiating PDN that receives a match acknowledgement naming its own player UUID SHALL clear the pending match and SHALL NOT enter the match countdown. A PDN SHALL NOT store a match record whose two player UUIDs are equal.

#### Scenario: Initiator refuses an acknowledgement that names its own player
- **WHEN** a Hunter registered as player $P$ has sent a match initiation and receives, from the Connected opponent, a match acknowledgement for that match carrying player UUID $P$
- **THEN** the Hunter clears the pending match, does not enter the match countdown, and stores no match record

#### Scenario: Invited PDN refuses an initiation that names its own player
- **WHEN** a Bounty registered as player $P$ receives a match initiation carrying player UUID $P$ from a Connected PDN on its opponent jack
- **THEN** the Bounty ignores the initiation and no match is primed

#### Scenario: No match record names the same player twice
- **WHEN** a match resolves on a PDN and the Hunter slot's player UUID equals the Bounty slot's player UUID
- **THEN** the PDN stores no match record for that match

#### Scenario: Demo players are not refused
- **WHEN** a demo Hunter with a nil player UUID receives a match acknowledgement carrying a nil player UUID
- **THEN** the same-player refusal does not apply, and the match proceeds as the other match requirements allow
