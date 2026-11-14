## ADDED Requirements

### Requirement: No registration PIN on the PDN

The session player provider SHALL hold no registration PIN. Clearing it SHALL drop everything "Clear registration session" lists except a PIN, and SHALL leave what that requirement leaves.

#### Scenario: Provider has no PIN to hold
- **WHEN** a profile delivered by an FDN is applied through the session player provider
- **THEN** the provider holds the profile and record key and no PIN
