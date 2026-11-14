## ADDED Requirements

### Requirement: Record key

A PDN SHALL hold at most one record key: `32` bytes that the server derives from the PDN's MAC address and the player the PDN is registered to, received during registration (`player-provisioning`). The PDN SHALL hold the record key in volatile memory only, for the session, and SHALL NOT write it to non-volatile storage. The PDN SHALL NOT send the record key over the peer-to-peer radio and SHALL NOT log or display it.

#### Scenario: Record key does not survive a restart
- **WHEN** a PDN holding a record key restarts
- **THEN** it holds no record key until it is registered again

#### Scenario: A PDN that has not been registered holds no record key
- **WHEN** a PDN boots and a player enters the demo PIN `0000`
- **THEN** the PDN holds no record key for that session

#### Scenario: Record key is not written to non-volatile storage
- **WHEN** a PDN accepts a record key during registration
- **THEN** no copy of the record key is in the PDN's non-volatile storage

### Requirement: Record tag

When a match ends and a PDN stores its match record, a PDN holding a record key SHALL compute a record tag: the first `16` bytes of HMAC-SHA-256 under that record key over the `112` match record bytes. A PDN holding no record key SHALL use `16` zero bytes, which mean no tag. The PDN SHALL store the record tag with the match record and SHALL send it unchanged whenever it sends that record. A crash record SHALL carry no record tag.

#### Scenario: Match record is tagged when the match ends
- **WHEN** a match ends on a PDN holding a record key and the PDN stores the match record
- **THEN** the PDN stores with it the first `16` bytes of HMAC-SHA-256 under that record key over the `112` match record bytes

#### Scenario: Match record stored without a record key has the all-zero tag
- **WHEN** a match ends on a PDN holding no record key and the PDN stores the match record
- **THEN** the PDN stores `16` zero bytes as its record tag and sends the record with them

#### Scenario: A stored tag is unchanged after the PDN is registered to another player
- **WHEN** a PDN stores a tagged match record, restarts, is registered to a different player, and then sends that record
- **THEN** the record is sent with the record tag stored when the match ended

#### Scenario: A changed record no longer matches its tag
- **WHEN** one bit of a match record's `112` bytes is changed after its record tag was computed
- **THEN** a holder of the record key recomputing the record tag obtains a different value

#### Scenario: Crash record carries no tag
- **WHEN** a PDN holding a record key sends a crash record
- **THEN** the crash record is sent with no record tag
