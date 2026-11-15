## MODIFIED Requirements

### Requirement: Post-match statistics, persistence, and result display

When a match resolves with a win or loss outcome:

- The device SHALL increment the player's total matches played, and either increment wins and win streak (on a win) or increment losses and reset win streak to 0 (on a loss).
- **Standalone match**:
  - The finalized match record (storing the winner player UUID decided by effective draw time alongside both players' raw reaction times) SHALL be appended to the 32-slot non-volatile match ring buffer for subsequent FDN collection, subject to `match-collection`'s rules: a match with a nil player UUID, the same player UUID on both sides, or no winner both devices agree on is not stored, nor is any match while 32 records are pending.
  - The device SHALL display the win or loss result screen for **8000 ms**, showing the player's effective draw time, the opponent's effective draw time, the number of posse supporter boosts applied, and any early-press countdown penalty incurred by the player, while broadcasting a posse win or loss event to its posse supporters.
  - After 8000 ms on the win or loss screen, the device SHALL pass through the dormant sleep and awaken screens before returning to idle.
- **Shootout match**:
  - The match SHALL NOT be written to non-volatile match storage and SHALL NOT trigger sleep; the winner reports the match result to the tournament and enters the between-matches spectator screen, and the loser enters the eliminated screen.

#### Scenario: Standalone win saves match for FDN collection, notifies posse, and sleeps
- **WHEN** a challenger wins a standalone match against a different registered player, both devices agree on the winner, and fewer than 32 records are pending
- **THEN** the finalized match record is saved to the 32-slot non-volatile match ring buffer for FDN collection, a posse win event is broadcast to its posse supporters, the win screen displays for 8 seconds, and the device proceeds through sleep and awaken before returning to idle

#### Scenario: Result screen surfaces early-press countdown penalties alongside posse boosts
- **WHEN** a player incurs 2 early-press penalties ($150\text{ ms}$) during the countdown and finishes a standalone match
- **THEN** the 8-second win or loss result screen displays the early-press penalty alongside the player's effective draw time, opponent's effective draw time, and posse supporter boosts

#### Scenario: Shootout match skips storage and sleep
- **WHEN** a paired player wins or loses a shootout match
- **THEN** no match record is written to non-volatile storage and the player transitions directly to the shootout spectator or eliminated state
