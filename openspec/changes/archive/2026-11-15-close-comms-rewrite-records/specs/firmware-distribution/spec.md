## ADDED Requirements

### Requirement: Release flashing preserves non-volatile storage

No release flash target or flash script step SHALL erase non-volatile storage or the whole flash by default, so stored match and crash records survive a USB or peer-to-peer firmware update. An explicit operator erase option MAY exist and SHALL be documented as wiping stored records.

#### Scenario: Release multi-flash over stored matches
- **WHEN** a release image is multi-flashed over a PDN holding 3 pending match records
- **THEN** the PDN still holds the 3 pending match records after boot
