## ADDED Requirements

### Requirement: Per-PDN record key

A PDN SHALL hold at most one record key: `32` bytes with a key ID in `1..255`, obtained during registration (`player-provisioning`). Key ID `0` SHALL mean the PDN holds no record key. The PDN SHALL keep the record key and its key ID in non-volatile storage across boots, SHALL NOT send the record key over the peer-to-peer radio, and SHALL NOT log or display it.

#### Scenario: Record key survives a reboot
- **WHEN** a PDN holding a record key with key ID 3 restarts
- **THEN** it still holds the same record key and key ID 3 before it is registered again

#### Scenario: A PDN that was never given a record key has key ID 0
- **WHEN** a PDN that has never received a record key boots
- **THEN** its key ID is `0`

### Requirement: A stored record key is not replaced

A PDN that holds a record key SHALL ignore any record key offered to it. A stored record key SHALL be removed only by an operator acting on that PDN directly, after which the PDN's key ID is `0` again.

#### Scenario: A second key offered to a PDN that has one is ignored
- **WHEN** a PDN holding a record key with key ID 1 is offered a different record key during registration
- **THEN** it keeps the record key it had and its key ID stays `1`

#### Scenario: Operator clears the key
- **WHEN** an operator removes the record key from a PDN
- **THEN** the PDN reports key ID `0` at its next registration and accepts the record key it is then given

### Requirement: Record tag

A PDN holding a record key SHALL compute a record tag for each crash record or match record it sends: the first `8` bytes of HMAC-SHA-256 under the record key over, in order, the record kind (one byte: `0x01` crash record, `0x02` match record), the PDN's 6-byte MAC address, the key ID, and the record bytes. The record tag SHALL be computed when the record is sent, with the record key held at that moment.

#### Scenario: The same bytes tagged as a different record kind give a different tag
- **WHEN** a PDN computes record tags over identical record bytes once as a crash record and once as a match record
- **THEN** the two record tags differ

#### Scenario: A changed record no longer matches its tag
- **WHEN** one bit of a sent record's bytes, the sender's MAC address or the key ID is changed after the record tag was computed
- **THEN** a holder of the record key recomputing the record tag obtains a different value

### Requirement: Record trailer

Every record a PDN sends SHALL be followed by a `9`-byte record trailer: the key ID (`1` byte) then the record tag (`8` bytes). A PDN with no record key SHALL send key ID `0` then `8` zero bytes. The record bytes the tag covers SHALL be the `42`-byte crash record or the `112`-byte match record exactly as sent; the record number in the frame that carries the record is not covered.

#### Scenario: Crash record from a PDN with a record key
- **WHEN** a PDN holding a record key with key ID 2 sends a crash record
- **THEN** the `42` record bytes are followed by key ID `2` and the `8`-byte record tag

#### Scenario: Crash record from a PDN with no record key
- **WHEN** a PDN with key ID `0` sends a crash record
- **THEN** the `42` record bytes are followed by key ID `0` and `8` zero bytes

### Requirement: Match records wait for a record key

A PDN whose key ID is `0` SHALL keep its match records pending and SHALL NOT send them. A PDN SHALL send crash records whether or not it holds a record key.

#### Scenario: Pending match records are held until the PDN has a key
- **WHEN** a PDN with key ID `0` and two pending match records hears an FDN's beacon
- **THEN** it sends no match record; once it has received a record key it sends them, on the next FDN beacon or FDN connection, with that key's ID and record tags

#### Scenario: A crash before the first registration is still reported
- **WHEN** a PDN that has never held a record key has a pending crash record and hears an FDN's beacon
- **THEN** it sends the crash record with key ID `0`
