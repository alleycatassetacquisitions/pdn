## ADDED Requirements

### Requirement: Band UID read

Each FDN SHALL have a reader for ISO/IEC 14443-A bands and cards. The FDN SHALL read only the UID that anticollision returns, `4` or `7` bytes, and SHALL NOT authenticate to, read or write any block, so the FDN holds no card keys. A band that stays in the reader's field SHALL count as one tap until the reader stops seeing it.

#### Scenario: Band held at the reader is one tap
- **WHEN** a band is held in the reader's field for `5000 ms`
- **THEN** the FDN reports one tap with that band's UID

#### Scenario: No card data is read
- **WHEN** a MIFARE Classic band is tapped
- **THEN** the FDN reports its UID and performs no sector authentication, read or write

### Requirement: Reader that does not respond

The FDN SHALL check its reader at boot and while a registration is pending. While the reader does not answer, the FDN SHALL report it as absent to the registration screen (`fdn-apps`) until it answers again, and SHALL run every other app as usual.

#### Scenario: Reader absent at boot
- **WHEN** an FDN boots with no reader answering
- **THEN** the FDN reports the reader absent, mounts its home app and runs symbol match and uploads as usual

#### Scenario: Reader that answers again
- **WHEN** an absent reader answers while a registration is pending
- **THEN** the FDN reports the reader present and accepts taps for the pending registration

### Requirement: Band reader behind a driver interface

The FDN SHALL reach its band reader only through a band reader driver interface in its driver framework, reporting either a latched UID or that the reader does not answer, so the registration logic does not depend on the reader model and native tests run against a fake reader.

#### Scenario: Native tests use a fake reader
- **WHEN** the FDN registration tests run natively with a fake band reader that reports a UID
- **THEN** the FDN handles it as a tap, as from the hardware reader
