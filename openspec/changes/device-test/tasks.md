## 1. Operator test flow (tester PDN)

- [ ] 1.1 DeviceTestApp state machine, ESP-NOW session, operator prompts, and server upload (in progress in firmware)

## 2. DUT subject mode

- [ ] 2.1 DeviceTestSubjectState handles tester commands and reports session info / button events

## 3. Result persistence (DUT-owned data)

- [ ] 3.1 After test completes on the DUT, persist the final `DeviceTestResult` (or encoded equivalent) to NVS via the existing prefs/storage driver; survive reboot; read path TBD (local display, later upload, or factory tooling)

## 4. Verification

- [ ] 4.1 Native tests for codec and any new storage adapter
- [ ] 4.2 Manual: complete test on DUT, reboot, confirm NVS record still present
