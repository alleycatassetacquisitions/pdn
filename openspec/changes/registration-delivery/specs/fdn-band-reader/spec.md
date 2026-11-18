## ADDED Requirements

### Requirement: Band UID read

An FDN SHALL read a band only by the UID that ISO/IEC 14443-A anticollision returns, `4`, `7` or `10` bytes, and SHALL NOT authenticate to, read or write any block, so the FDN holds no card keys. A band that stays in the reader's field SHALL count as one tap until the reader stops seeing it.

#### Scenario: Band held at the reader is one tap
- **WHEN** a band is held in the reader's field for `5000 ms`
- **THEN** the reader reports one tap with that band's UID

#### Scenario: No card data is read
- **WHEN** a MIFARE Classic band is tapped
- **THEN** the reader reports its UID and performs no sector authentication, read or write

### Requirement: Band reader behind a driver interface

The FDN SHALL reach its band reader only through a band reader driver interface in its driver framework, registered under the band reader driver name, which reports a latched UID and whether the reader answers. Registration logic SHALL NOT depend on the reader model, and native tests SHALL run against a fake reader.

#### Scenario: Fake reader found through the driver framework
- **WHEN** a fake band reader is registered under the band reader driver name and a band is placed on it
- **THEN** the reader found by that name reports the tap with the band's UID

### Requirement: A reader-less FDN registers nobody

No band reader hardware driver exists until the reader part and its pins are confirmed (#485 Q1), so an FDN image SHALL register no reader. The absence SHALL be observable: looking the reader up by its driver name finds none. An FDN without a reader SHALL register no PDN.

#### Scenario: Release FDN has no reader
- **WHEN** an FDN boots from a release image
- **THEN** looking up the band reader by its driver name finds none, and the FDN registers no PDN
