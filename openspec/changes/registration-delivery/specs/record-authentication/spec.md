## ADDED Requirements

### Requirement: Record key

A PDN SHALL hold at most one record key: the `32` bytes the server derives for the player the PDN is registered to, delivered during registration (`player-provisioning`). The PDN SHALL hold it in volatile memory only, SHALL NOT write it to its key-value storage or match ring, SHALL NOT send it to any device and SHALL NOT log or display it. Clearing the registered player SHALL drop the key and overwrite its bytes.

#### Scenario: Record key is not written to storage
- **WHEN** a PDN holding a record key stores a match record
- **THEN** no copy of the record key is in its key-value storage or its match ring

#### Scenario: Record key does not survive a restart
- **WHEN** a PDN holding a record key restarts
- **THEN** it holds no record key until it is registered again

#### Scenario: Record key is dropped with the player
- **WHEN** a PDN holding a record key clears its registered player
- **THEN** it holds no record key and the key's bytes are overwritten in memory

### Requirement: Record tag

When a PDN stores a match record it SHALL compute its record tag once: the first `16` bytes of HMAC-SHA-256 under the record key it holds over the `112` match record bytes, stored with the record. A stored tag SHALL be sent as stored and never recomputed, whatever key the PDN holds later. A crash record SHALL carry no tag.

#### Scenario: Match record is tagged when the match ends
- **WHEN** a match ends on a PDN holding a record key and the PDN stores the match record
- **THEN** it stores with it the first `16` bytes of HMAC-SHA-256 under that key over the `112` record bytes

#### Scenario: Record tag equals the shared test vector
- **WHEN** a PDN holding the record key `00 01 02 … 1f` (`32` bytes, each equal to its index) stores a match record whose `112` bytes are `00 01 02 … 6f`
- **THEN** the stored record tag is `a9 cc 05 f6 42 50 6d 83 89 c1 fc 42 70 6e 7c 99`

#### Scenario: A stored tag survives registering another player
- **WHEN** a PDN stores a tagged match record, restarts, is registered to a different player and then sends that record
- **THEN** the record is sent with the tag stored when the match ended

#### Scenario: Crash record carries no tag
- **WHEN** a PDN holding a record key sends a crash record
- **THEN** the crash record is sent with no tag

### Requirement: Match with no record key

A practice match is never stored (`quickdraw-match`, #498). A scored match that ends on a PDN holding no record key SHALL be stored and relayed as it would otherwise, with a tag of `16` zero bytes; the server judges it.

#### Scenario: Match without a key is stored with a zero tag
- **WHEN** a scored match between two distinct non-nil player UUIDs ends on a PDN that holds no record key
- **THEN** the PDN stores the match record with a tag of `16` zero bytes and relays it like any other

#### Scenario: Practice match is not stored even with a key
- **WHEN** a practice match ends on a PDN that holds a record key
- **THEN** the PDN stores no match record

### Requirement: Sender's MAC address is a note only

The sender MAC an FDN stores with a relayed record is the radio source address of its frame, which any device can set. The FDN SHALL NOT accept, refuse or retire a record because of that address. Sending it with an upload waits on the server's upload format (#486 Q3).

#### Scenario: A record relayed under another PDN's address is stored like any other
- **WHEN** an FDN receives a well-formed match record frame whose radio source address is that of a PDN other than the one that stored the record
- **THEN** the FDN stores and acknowledges it with its bytes and tag unchanged
