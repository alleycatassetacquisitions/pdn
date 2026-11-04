## ADDED Requirements

### Requirement: Nodes Created persistence
NodeNetwork SHALL persist a `Nodes Created` counter and the set of peer MAC addresses that have already incremented it after successful upload.

#### Scenario: Storage namespace
- **WHEN** stats are written
- **THEN** they use `NODE_NETWORK_STATS_PREFS_NAMESPACE` (`nodenet`)
