## MODIFIED Requirements

### Requirement: Bounded persistent match ring buffer and collection watermark on PDN

Every completed standalone 1v1 match SHALL be persisted on the PDN in a bounded non-volatile ring buffer in binary form, in its own storage area, alongside a monotonic sequence counter and collection watermark:

- Each persisted match SHALL increment the persisted `match_sequence_number` by `1` (starting at `1`) and write the binary match record, which names both player UUIDs, followed by its `16`-byte record tag computed under the record key held when the match ended (`record-authentication`), into a `32`-slot circular buffer indexed by `(match_sequence_number - 1) mod 32`. A slot that does not hold a whole record and tag SHALL NOT be sent; it is passed over like any slot that cannot be read.
- Binary match records containing raw 16-byte UUIDs (which may include `0x00` bytes) SHALL round-trip through non-volatile storage without null-byte truncation.
- While `32` matches are pending, the PDN SHALL NOT persist a new match and SHALL log the refusal; a pending match is never overwritten, and gameplay is not blocked.
- A shootout match SHALL NOT be persisted. Every other finalized match SHALL be persisted as recorded, including one with a nil player UUID, the same player UUID on both sides, or no agreed winner; the server rejects or flags those.
- The PDN SHALL persist a separate `collected_high_water_mark` (`0` initially), clamped to `match_sequence_number` if larger. Match `N` is pending whenever `N > collected_high_water_mark` and `N > match_sequence_number - 32`. Advancing the mark to `M` marks matches `<= M` as collected and leaves newer matches pending.

#### Scenario: 33rd match overwrites oldest slot without losing newer uncollected matches
- **WHEN** a PDN has 32 pending matches (`1..32`) and finishes a 33rd standalone match before encountering an FDN
- **THEN** the 33rd match is not stored, `match_sequence_number` stays `32`, and matches `1..32` remain pending in ascending order

#### Scenario: Binary UUIDs containing zero bytes round-trip intact through storage
- **WHEN** a match record whose 16-byte match UUID, player UUIDs, or team UUIDs contain `0x00` bytes is written to and read back from non-volatile storage
- **THEN** all 16 bytes of every UUID and all numeric fields read back identically without null-byte truncation

#### Scenario: A slot without a tag is not sent
- **WHEN** a PDN's ring holds, in the slot of pending match `1`, a `112`-byte record with no tag written by an earlier test build, and match `2` stored with its tag
- **THEN** on an FDN beacon the PDN sends only match `2`
