## ADDED Requirements

### Requirement: DUT persists completed test results in NVS
When a device test session completes on the DUT, the device SHALL write the final test result record to non-volatile storage (NVS) so the unit retains its own manufacturing data locally. The stored record SHALL survive reboot and SHALL be owned by the DUT (not only held on the tester or uploaded to the server).

#### Scenario: Session ends on DUT
- **WHEN** the DUT receives `END_SESSION` (or otherwise leaves device-test subject mode with a completed result)
- **THEN** the device persists the result to NVS before returning to normal pairing flow
- **AND** the persisted data includes component pass/fail outcomes and identity fields needed for later readout or upload

#### Scenario: Reboot after test
- **WHEN** the DUT reboots after a completed test
- **THEN** the last stored test result remains available from NVS
