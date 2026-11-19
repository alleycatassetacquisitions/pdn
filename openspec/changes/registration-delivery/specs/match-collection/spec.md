## MODIFIED Requirements

### Requirement: Bounded persistent match ring buffer and collection watermark on PDN

Every completed scored standalone 1v1 match (`quickdraw-match`) SHALL be persisted on the PDN in a bounded non-volatile ring buffer in binary form, in its own storage area, alongside a monotonic sequence counter and collection watermark:

- Each persisted match SHALL increment the persisted `match_sequence_number` by `1` (starting at `1`) and write the binary match record, which names both player UUIDs, followed by its `16`-byte record tag computed under the record key held when the match ended (`record-authentication`), into a `32`-slot circular buffer indexed by `(match_sequence_number - 1) mod 32`. A slot that does not hold a whole record and tag SHALL NOT be sent; it is passed over like any slot that cannot be read.
- Binary match records containing raw 16-byte UUIDs (which may include `0x00` bytes) SHALL round-trip through non-volatile storage without null-byte truncation.
- While `32` matches are pending, the PDN SHALL NOT persist a new match and SHALL log the refusal; a pending match is never overwritten, and gameplay is not blocked.
- A shootout match SHALL NOT be persisted, nor SHALL a practice match: one with a nil player UUID or the same player UUID on both sides (`quickdraw-match`). Every other finalized match SHALL be persisted as recorded, including one with no agreed winner; the server flags it.
- The PDN SHALL persist a separate `collected_high_water_mark` (`0` initially), clamped to `match_sequence_number` if larger. Match `N` is pending whenever `N > collected_high_water_mark` and `N > match_sequence_number - 32`. Advancing the mark to `M` marks matches `<= M` as collected and leaves newer matches pending.
- The ring's own storage area SHALL be a flash range after every partition of the release partition table, which the PDN registers at boot when the flashed table lacks it, so no deployed table needs a USB flash; only a test build's table lists it. At boot the PDN SHALL move the pending records of a ring kept in the default storage into the area, oldest first, keeping their numbers when the area's ring is empty, and then erase the old ring. A move cut short by power loss SHALL leave each record in at least one place and finish on the next boot without storing a record twice. Records that do not fit a full area SHALL stay pending where they were until it has room. If the range cannot be registered or opened, the ring SHALL stay in the default storage. Neither path opens, writes or clears the `matches` namespace.

#### Scenario: 33rd match overwrites oldest slot without losing newer uncollected matches
- **WHEN** a PDN has 32 pending matches (`1..32`) and finishes a 33rd standalone match before encountering an FDN
- **THEN** the 33rd match is not stored, `match_sequence_number` stays `32`, and matches `1..32` remain pending in ascending order

#### Scenario: Binary UUIDs containing zero bytes round-trip intact through storage
- **WHEN** a match record whose 16-byte match UUID, player UUIDs, or team UUIDs contain `0x00` bytes is written to and read back from non-volatile storage
- **THEN** all 16 bytes of every UUID and all numeric fields read back identically without null-byte truncation

#### Scenario: A slot without a tag is not sent
- **WHEN** a PDN's ring holds, in the slot of pending match `1`, a `112`-byte record with no tag written by an earlier test build, and match `2` stored with its tag
- **THEN** on an FDN beacon the PDN sends only match `2`

#### Scenario: Pending records move to the ring's own area at boot
- **WHEN** a PDN boots with its ring's area available and its default storage holding matches `1..5` with `collected_high_water_mark` `2`
- **THEN** matches `3..5` are in the area under the same numbers with their tags, the default storage holds no ring entry, the `matches` namespace is byte-identical, and on the next FDN beacon the PDN sends match `3` first

#### Scenario: Ring stays in the default storage when its area cannot be registered
- **WHEN** a PDN boots on a partition table one of whose partitions overlaps the ring's range
- **THEN** the PDN stores and sends matches from the default storage as before, and the ring kept there is not erased

### Requirement: PDN-to-FDN match transfer, FDN spool bounds, and watermark advancement

A PDN with pending match records SHALL deliver them to any FDN it hears by beacon, Connected on a jack or not, as record frames (`record-relay`):

- The PDN SHALL send pending match records oldest first, from `max(collected_high_water_mark + 1, match_sequence_number - 31)` to `match_sequence_number`.
- The PDN SHALL advance its persisted `collected_high_water_mark` to a match's sequence number only on that FDN's stored acknowledgement for the record (`record-relay`), never on an acknowledgement from any other sender.
- The FDN SHALL stage received match records in a capacity-bounded non-volatile store of up to `27` match records awaiting upload (`139` bytes each with the sender and tag, sized to the default NVS partition). If the store is full, the FDN SHALL withhold the stored acknowledgement so the PDN keeps the records pending.
- If contact ends before all pending matches are acknowledged, `collected_high_water_mark` SHALL reflect the last acknowledged match so only the rest are sent on the next contact.

#### Scenario: Partial FDN collection advances watermark only for acknowledged matches
- **WHEN** a PDN has pending matches `1..5` and leaves an FDN's range after the FDN acknowledges matches `1`, `2`, and `3`
- **THEN** the PDN's `collected_high_water_mark` advances to `3`, and on the next FDN beacon the PDN sends only matches `4` and `5`

#### Scenario: Spoofed wireless acknowledgement from uncabled sender is ignored
- **WHEN** a PDN that sent match `1` to an FDN receives a stored acknowledgement for match `1` from a different sender MAC
- **THEN** the PDN ignores the acknowledgement and leaves `collected_high_water_mark` unchanged

