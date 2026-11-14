## ADDED Requirements

### Requirement: Record key

A PDN SHALL hold at most one record key: the `32` bytes the server returns for the player the PDN is being registered to, received during registration (`player-provisioning`). The PDN SHALL hold the record key in volatile memory and SHALL NOT write it to its key-value storage or its match ring buffer. The PDN SHALL NOT send the record key to any device and SHALL NOT log or display it.

#### Scenario: A PDN playing with the demo profile holds no record key
- **WHEN** a PDN boots and a player enters the demo PIN `0000`
- **THEN** the PDN holds no record key for that session

#### Scenario: A registered PDN holds a record key
- **WHEN** a PDN applies a profile delivered by an FDN
- **THEN** the PDN holds the record key it accepted during that registration

#### Scenario: Record key is not written to the PDN's key-value storage or match ring buffer
- **WHEN** a PDN accepts a record key during registration
- **THEN** no copy of the record key is in the PDN's key-value storage or its match ring buffer

### Requirement: Record key lifetime

A record key SHALL last no longer than the session in which the PDN accepted it. A record key accepted later in the same registration attempt SHALL replace the one the PDN holds. A PDN SHALL drop the record key it holds whenever a registration attempt ends without a profile applied.

#### Scenario: Record key does not survive a restart
- **WHEN** a PDN holding a record key restarts
- **THEN** it holds no record key until it is registered again

#### Scenario: A later record key in the same registration attempt replaces the held one
- **WHEN** a PDN that accepted a record key receives a second record key frame before the profile arrives, and accepts it
- **THEN** the PDN holds only the second record key

#### Scenario: Record key is dropped when registration ends without a profile
- **WHEN** a PDN accepts a record key and then shows the lookup failure screen because no profile was applied
- **THEN** the PDN holds no record key

### Requirement: Record tag

When a match ends and a PDN stores its match record, the PDN SHALL compute a record tag: the first `16` bytes of HMAC-SHA-256 under the record key it holds over the `112` match record bytes. The PDN SHALL store the record tag with the match record. A crash record SHALL carry no record tag.

#### Scenario: Match record is tagged when the match ends
- **WHEN** a match ends on a PDN holding a record key and the PDN stores the match record
- **THEN** the PDN stores with it the first `16` bytes of HMAC-SHA-256 under that record key over the `112` match record bytes

#### Scenario: Record tag equals the shared test vector
- **WHEN** a PDN holding the record key `00 01 02 … 1f` (`32` bytes, each equal to its index) stores a match record whose `112` bytes are `00 01 02 … 6f` (each equal to its index)
- **THEN** the stored record tag is `a9 cc 05 f6 42 50 6d 83 89 c1 fc 42 70 6e 7c 99`

#### Scenario: Crash record carries no tag
- **WHEN** a PDN holding a record key sends a crash record
- **THEN** the crash record is sent with no record tag

### Requirement: No match record without a record tag

A PDN SHALL store a match record only together with a record tag computed under the record key it holds. A PDN that holds no record key when a match ends SHALL NOT store a match record for that match. No record tag value SHALL stand for a missing tag.

#### Scenario: Match on a PDN holding no record key is not stored
- **WHEN** a match ends on a PDN playing with the demo profile, which holds no record key
- **THEN** the PDN stores no match record for that match

### Requirement: Stored record tags are not changed

A PDN SHALL send a stored match record with the record tag stored when the match ended. Restarting the PDN, a registration that fails, and registering the PDN to the same or a different player SHALL NOT remove, alter or recompute a stored match record or its record tag.

#### Scenario: A stored tag is unchanged after the PDN is registered to another player
- **WHEN** a PDN stores a tagged match record, restarts, is registered to a different player, and then sends that record
- **THEN** the record is sent with the `112` record bytes and the record tag stored when the match ended

#### Scenario: A stored tag is sent by a PDN that is not registered
- **WHEN** a PDN stores a tagged match record, restarts, and sends that record before any player has registered on it
- **THEN** the record is sent with the record tag stored when the match ended
