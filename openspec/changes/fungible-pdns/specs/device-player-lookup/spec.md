## ADDED Requirements

### Requirement: Optional scoring totals are decoded

On a successful response, the client SHALL also decode the optional scoring totals of `DevicePlayerResponse`: matches played, wins and average draw time in milliseconds, each an unsigned 32-bit value. The `Player` passed to the success callback SHALL hold each total that is present and `0` for each that is absent. Absent totals SHALL NOT make the response undecodable.

#### Scenario: Response with totals
- **WHEN** the response contains matches played `12`, wins `7` and average draw time `300`
- **THEN** the `Player` reports totals of `12` matches played, `7` wins and an average draw time of `300 ms`

#### Scenario: Response without totals
- **WHEN** the response contains a valid player and none of the three totals
- **THEN** the success callback receives the `Player` with totals of `0`, `0` and `0 ms`
- **AND** the error callback is not called

#### Scenario: Response with only some totals
- **WHEN** the response contains matches played `4` and neither wins nor average draw time
- **THEN** the `Player` reports `4` matches played, `0` wins and an average draw time of `0 ms`
