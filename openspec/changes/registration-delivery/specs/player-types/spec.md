## MODIFIED Requirements

### Requirement: alleycat-server decodes into Player
`DevicePlayerCodec` and `PlayerApi::getDevicePlayerByBand` SHALL decode into the registration fields on `Player`. The vendored `DevicePlayerResponse` SHALL carry `optional bytes record_key = 10`, marked `SERVER-SEAM` until the server team agrees it, so the vendored proto differs from the alleycat-rs commit in `upstream.lock.json`; the nanopb outputs SHALL be regenerated from it. `PlayerLimits` buffer sizes SHALL match `device_api.options`, enforced at compile time.

#### Scenario: Decoded player
- **WHEN** `DevicePlayerCodec` decodes a valid `DevicePlayerResponse` body into a `Player`
- **THEN** the `Player` registration fields hold id, name, team, and affiliations from the message
- **AND** `hasRegistrationProfile()` is true
