## MODIFIED Requirements

### Requirement: Post-match statistics, persistence, and result display

When a match resolves with a win or loss outcome:

- The device SHALL increment the player's total matches played, and either
  increment wins and win streak (on a win) or increment losses and reset win
  streak to 0 (on a loss).
- **Standalone match**:
  - The finalized match record SHALL be appended to non-volatile match storage
    on the PDN. The record holds the match ID, each player's server player ID
    and team ID, each player's device MAC, the winner's server player ID, both
    effective draw times, and the UTC time the match finalized.
  - Match storage SHALL hold the 32 most recent records. Appending a 33rd
    overwrites the oldest, whether or not it was collected.
  - The device SHALL display the win or loss result screen for **8000 ms**,
    showing the player's effective draw time, the opponent's effective draw
    time, and the number of posse supporter boosts applied, while broadcasting
    a posse win or loss event to its posse supporters.
  - After 8000 ms on the win or loss screen, the device SHALL pass through the
    dormant sleep and awaken screens and return to idle. The PDN SHALL NOT
    upload matches itself; a cabled FDN collects them (see `match-collection`).
- **Shootout match**:
  - The match SHALL NOT be written to non-volatile match storage and SHALL NOT
    trigger sleep; the winner reports the match result to the tournament and
    enters the between-matches spectator screen, and the loser enters the
    eliminated screen.

#### Scenario: Standalone win saves match, notifies posse, and sleeps
- **WHEN** a challenger wins a standalone match
- **THEN** the match is saved to non-volatile storage, a posse win event is broadcast to its posse supporters, the win screen displays for 8 seconds, and the device proceeds through sleep and awaken back to idle without enabling Wi-Fi

#### Scenario: Ring overwrites the oldest record
- **WHEN** a PDN that already holds 32 stored matches finalizes another standalone match
- **THEN** the new record replaces the oldest one and the PDN still holds 32 records

#### Scenario: Shootout match skips storage
- **WHEN** a paired player wins or loses a shootout match
- **THEN** no match record is written to non-volatile storage and the player transitions directly to the shootout spectator or eliminated state
