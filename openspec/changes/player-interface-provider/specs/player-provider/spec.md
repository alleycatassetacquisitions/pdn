## ADDED Requirements

### Requirement: Session player provider owns a single player instance
The session player provider SHALL own exactly one gameplay player instance for the lifetime of the device session.

#### Scenario: Session player always exists
- **WHEN** the session player provider has been constructed
- **THEN** querying the provider returns the same player instance for the lifetime of the provider

### Requirement: Optional registration profile on session player
Whether a server registration profile is present SHALL be tracked on the owned session player and reflected by the provider's registration-present query.

#### Scenario: Empty session
- **WHEN** the session player provider has been constructed and no registration profile has been applied
- **THEN** the provider reports that no registered player profile is present

#### Scenario: Profile applied
- **WHEN** a registration profile is stored on the owned session player
- **THEN** the provider reports that a registered player profile is present

### Requirement: Clear registration session
Clearing the session player provider SHALL drop the registration profile on the owned player and reset the player identity to the unprovisioned nil UUID state.

#### Scenario: Clear
- **WHEN** the session player provider is cleared after a profile was applied
- **THEN** the provider reports that no registered player profile is present
- **AND** the player's registration profile is cleared

### Requirement: Provisioning app uses the session player provider
The PDN provisioning app SHALL read and update registration fields (player UUID, display name, team, and affiliations) exclusively through the shared session player provider instance shared with gameplay apps.

#### Scenario: Provisioning success path
- **WHEN** a player profile is delivered from a Connected FDN
- **THEN** the decoded profile is applied to the shared session player's registration fields
- **AND** the welcome screen reads the player's display name from the applied registration profile

#### Scenario: Shared session player across provisioning and gameplay apps
- **WHEN** firmware initializes the provisioning app and gameplay apps
- **THEN** both operate on the same session player instance owned by the session player provider
