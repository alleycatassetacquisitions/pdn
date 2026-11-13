## ADDED Requirements

### Requirement: Per-PDN record key

A PDN SHALL hold at most one record key: `32` bytes with a key ID in `1..255`, obtained during registration (`player-provisioning`). Key ID `0` SHALL mean the PDN holds no record key. The PDN SHALL keep the record key and its key ID in non-volatile storage across boots, SHALL NOT send the record key over the peer-to-peer radio, and SHALL NOT log or display it.

#### Scenario: Record key survives a reboot
- **WHEN** a PDN holding a record key with key ID 3 restarts
- **THEN** it still holds the same record key and key ID 3 before it is registered again

#### Scenario: A PDN that was never given a record key has key ID 0
- **WHEN** a PDN that has never received a record key boots
- **THEN** its key ID is `0`

### Requirement: A record key accepted at registration replaces the stored one

When a PDN accepts a record key (`player-provisioning`) it SHALL store that key and its key ID in place of any it holds. If power is lost during the change the PDN SHALL afterwards hold either the earlier record key or the new one, whole, with its own key ID.

#### Scenario: Registering again replaces the record key
- **WHEN** a PDN holding a record key with key ID 1 accepts a record key with key ID 2 during registration
- **THEN** it holds the new record key with key ID `2` and no longer holds the earlier one

#### Scenario: A key ID lower than the one held is accepted
- **WHEN** a PDN holding a record key with key ID 255 accepts a record key with key ID 1 during registration
- **THEN** it holds the new record key with key ID `1`

#### Scenario: Power lost while the key is being replaced
- **WHEN** a PDN holding a record key with key ID 1 loses power while storing an accepted record key with key ID 2
- **THEN** on its next boot it holds either the earlier record key with key ID `1` or the new one with key ID `2`

### Requirement: Record tag

A PDN holding a record key SHALL compute a record tag for each crash record or match record it sends: the first `16` bytes of HMAC-SHA-256 under the record key over, in order, the record kind (one byte: `0x01` crash record, `0x02` match record), the PDN's 6-byte MAC address, the key ID, and the record bytes. The record tag SHALL be computed when the record is sent, with the record key held at that moment.

#### Scenario: The same bytes tagged as a different record kind give a different tag
- **WHEN** a PDN computes record tags over identical record bytes once as a crash record and once as a match record
- **THEN** the two record tags differ

#### Scenario: A pending record goes out under the new key
- **WHEN** a PDN that sent a crash record under key ID 1 without receiving its stored acknowledgement accepts a record key with key ID 2 and then sends that record again
- **THEN** the record is sent with key ID `2` and a record tag computed under the new record key

#### Scenario: A changed record no longer matches its tag
- **WHEN** one bit of a sent record's bytes, the sender's MAC address or the key ID is changed after the record tag was computed
- **THEN** a holder of the record key recomputing the record tag obtains a different value

### Requirement: Record trailer

Every record a PDN sends SHALL be followed by a `17`-byte record trailer: the key ID (`1` byte) then the record tag (`16` bytes). A PDN with no record key SHALL send key ID `0` then `16` zero bytes. The record bytes the tag covers SHALL be the `42`-byte crash record or the `112`-byte match record exactly as sent; the record number in the frame that carries the record is not covered.

#### Scenario: Crash record from a PDN with a record key
- **WHEN** a PDN holding a record key with key ID 2 sends a crash record
- **THEN** the `42` record bytes are followed by key ID `2` and the `16`-byte record tag

#### Scenario: Crash record from a PDN with no record key
- **WHEN** a PDN with key ID `0` sends a crash record
- **THEN** the `42` record bytes are followed by key ID `0` and `16` zero bytes

### Requirement: Match records wait for a record key

A PDN whose key ID is `0` SHALL keep its match records pending and SHALL NOT send them. A PDN SHALL send crash records whether or not it holds a record key.

#### Scenario: Pending match records are held until the PDN has a key
- **WHEN** a PDN with key ID `0` and two pending match records hears an FDN's beacon
- **THEN** it sends no match record; once it has received a record key it sends them, on the next FDN beacon or FDN connection, with that key's ID and record tags

#### Scenario: A crash before the first registration is still reported
- **WHEN** a PDN that has never held a record key has a pending crash record and hears an FDN's beacon
- **THEN** it sends the crash record with key ID `0`
