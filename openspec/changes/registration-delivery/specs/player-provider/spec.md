## MODIFIED Requirements

### Requirement: Clear registration session
Clearing the session player provider SHALL drop the registration profile on the owned player, reset the player identity to the unprovisioned nil UUID state, and drop and overwrite the record key it holds (`record-authentication`).

#### Scenario: Clear
- **WHEN** the session player provider is cleared after a profile and a record key were applied
- **THEN** the provider reports that no registered player profile is present
- **AND** the player's registration profile is cleared
- **AND** no record key is held and its bytes are overwritten

### Requirement: Provisioning app uses the session player provider
The PDN provisioning app SHALL read and update registration fields (player UUID, display name, team, and affiliations) and the record key exclusively through the session player provider, whose `Player` the gameplay apps also use.

#### Scenario: Provisioning success path
- **WHEN** a role is confirmed on role selection, or a profile and record key delivered by an FDN are applied
- **THEN** the profile is applied to the shared session player's registration fields, and a delivered key to the provider's record key

#### Scenario: Shared session player across provisioning and gameplay apps
- **WHEN** firmware initializes the provisioning app and gameplay apps
- **THEN** both operate on the same session player instance owned by the session player provider
