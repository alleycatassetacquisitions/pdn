# symbol-match Specification

## Purpose

Specify symbol match end-to-end: **FDN-hosted** dual-jack play, **offline** puzzle execution (requiring no live server round-trip during symbol matching or codeword reveal), **rotating symbols**, **dual-jack win**, **per-FDN fixed codeword** on success for **30 seconds**, then **reset**. On a PDN, symbol match is entered when an FDN is Connected on the PDN's **downstream jack**, exchanging symbols over the peer-to-peer radio and exiting non-blockingly on unplug.

## Requirements

### Requirement: FDN SHALL mount symbol match for puzzle play

When the FDN enters the symbol match application (either at boot when no other FDN flow is active or when routed from the FDN idle/connection flow for a provisioned PDN in `fdn-apps`), the symbol match state machine SHALL manage symbol selection, dual-jack play, and codeword reveal.

#### Scenario: Symbol match activation

- **GIVEN** an FDN entering the symbol match application
- **WHEN** the symbol match state machine mounts
- **THEN** the FDN starts at the selection phase and enters dual-jack symbol play

### Requirement: Symbol match gameplay SHALL run without a live backend round-trip

Active symbol play, codeword reveal, and round reset SHALL NOT depend on live HTTP requests or cloud API availability during the puzzle; any FDN background server synchronization (for collected matches and crash logs in `fdn-apps`, `match-collection`, and `crash-reporting`) runs outside active symbol play.

#### Scenario: No server connectivity during symbol match

- **GIVEN** the FDN has no usable server connection
- **WHEN** players play symbol match
- **THEN** symbol matching, codeword reveal, and round reset complete normally without network calls

### Requirement: FDN SHALL assign independent symbols per jack

The FDN SHALL hold one symbol per upstream jack from a shared **five-symbol** set. On each refresh, each new symbol SHALL differ from **both** symbols from the previous period.

#### Scenario: Symbol refresh

- **GIVEN** the FDN is drawing symbols for a new period
- **WHEN** left and right symbols are assigned
- **THEN** each is one of five symbols
- **AND** neither equals the previous left or previous right symbol

### Requirement: FDN SHALL rotate symbols every thirty minutes

During active play, the FDN SHALL run a **thirty-minute** refresh timer, then run the short selection phase and start a new period with new symbols.

#### Scenario: Timer expiry

- **GIVEN** play is active and the refresh timer is running
- **WHEN** thirty minutes elapse for the current period
- **THEN** the FDN runs selection and enters play with new symbols and a restarted timer

#### Scenario: Countdown display

- **GIVEN** play is active
- **WHEN** the main symbol screen updates
- **THEN** time remaining until refresh is shown as `MM:SS`

### Requirement: FDN SHALL use a brief selection phase before play

Before each play period, the FDN SHALL show a short loading presentation (approximately **one second**), assign symbols, and start the thirty-minute refresh timer.

#### Scenario: After boot or codeword reset

- **GIVEN** the FDN is starting a new period from boot or after codeword success
- **WHEN** selection completes
- **THEN** play begins with fresh symbols and a running refresh timer

### Requirement: FDN and PDN SHALL exchange symbols over ESP-NOW

Gameplay SHALL use the symbol-match wireless command set (see design reference). The FDN SHALL send its jack symbol to connected PDN peers; the FDN SHALL mark a jack matched when a received PDN symbol equals that jack’s local symbol.

#### Scenario: FDN publishes symbol

- **GIVEN** a PDN is connected on a jack with a resolved peer MAC
- **WHEN** play is active on that jack
- **THEN** the FDN sends its local symbol for that jack to the PDN

#### Scenario: FDN accepts a matching PDN symbol

- **GIVEN** play is active on a jack
- **WHEN** the FDN receives the PDN’s symbol for that jack
- **THEN** the jack is matched if and only if it equals the FDN’s local symbol for that jack
- **AND** UI reflects matched vs unmatched for that side

#### Scenario: Jack disconnect

- **GIVEN** a jack had match or send state
- **WHEN** the PDN disconnects from that jack
- **THEN** match state for that jack is cleared

#### Scenario: New symbol period

- **GIVEN** the FDN ends play for a symbol period (timer, or reset after codeword)
- **WHEN** connected PDNs are still present
- **THEN** the FDN notifies PDNs with the refresh command so clients reset UI for the new period

### Requirement: FDN SHALL win only when both jacks match

The FDN SHALL enter codeword success only when **both** jacks satisfy the full match rule (including that the FDN has sent its symbol to each connected jack used for win evaluation).

#### Scenario: Single jack matched

- **GIVEN** at most one jack is fully matched
- **WHEN** play continues
- **THEN** codeword success is not shown

#### Scenario: Both jacks matched

- **GIVEN** both jacks meet the full match rule
- **WHEN** the win condition is satisfied
- **THEN** the FDN enters codeword success
- **AND** the FDN sends round-success to connected PDN peers

### Requirement: Each FDN SHALL have one fixed codeword

Each FDN unit SHALL be configured with a single codeword or short phrase. That value SHALL NOT change when symbols rotate or when rounds reset.

#### Scenario: Repeated wins on one unit

- **GIVEN** an FDN configured with a specific codeword
- **WHEN** players win multiple times on that unit
- **THEN** every success screen shows the same codeword

### Requirement: FDN SHALL show codeword on a single success screen

On win, the FDN SHALL show **one** screen whose primary content is the unit codeword. No multi-step auth, asset ID, or menu flow SHALL be part of success.

#### Scenario: Win UI

- **GIVEN** the dual-jack win condition is met
- **WHEN** codeword success starts
- **THEN** the codeword is the main on-screen content

### Requirement: FDN SHALL display codeword for thirty seconds then reset

Codeword success SHALL last **thirty seconds**, then the FDN SHALL restart at the selection phase (new symbol period), not at main menu or hacking.

#### Scenario: Success timeout

- **GIVEN** codeword success is visible
- **WHEN** thirty seconds elapse
- **THEN** the FDN runs selection and begins a new play period

### Requirement: PDN SHALL enter symbol play when FDN is on its downstream jack

When a provisioned PDN in its idle state detects a Connected FDN on its **downstream jack**, it SHALL enter symbol play state, remain there while the downstream-jack FDN connection holds, and exit to idle without blocking the main loop when the downstream jack disconnects (never spinning in a blocking wait on dismount that could starve the `100 ms` serial link liveness window).

#### Scenario: FDN plugged into PDN downstream jack

- **GIVEN** a provisioned PDN in its idle state
- **WHEN** a Connected FDN is detected on the PDN's downstream jack
- **THEN** the PDN enters symbol play for that session

#### Scenario: FDN connected only on PDN upstream jack does not bounce into symbol play

- **GIVEN** a provisioned PDN in its idle state
- **WHEN** an FDN is Connected on the PDN's upstream jack and not on its downstream jack
- **THEN** the PDN remains in idle and does not bounce into symbol play

#### Scenario: FDN unplugged from PDN downstream jack exits non-blockingly

- **GIVEN** the PDN is in symbol play
- **WHEN** the FDN is no longer Connected on the PDN's downstream jack
- **THEN** the PDN leaves symbol play for idle without blocking the main loop on dismount

### Requirement: PDN SHALL compare symbols before committing send

The PDN SHALL treat the round as ready to send its symbol to the FDN only after it has received the FDN’s symbol for that jack and it **equals** the PDN player’s symbol. Sending SHALL use the designated player control (e.g. secondary button click).

#### Scenario: Matching symbols

- **GIVEN** the PDN received the FDN symbol for its jack
- **WHEN** that symbol equals the player’s symbol
- **THEN** the player can send their symbol to the FDN
- **AND** after send, UI indicates the committed state per existing symbol UX

#### Scenario: Non-matching symbols

- **GIVEN** the FDN symbol does not equal the player’s symbol
- **WHEN** the player attempts send
- **THEN** the PDN does not send a matching commit (rejection UX is implementation-defined)

### Requirement: PDN SHALL celebrate FDN round success

When the PDN receives round-success from the FDN, it SHALL enter a matched celebration state until a symbol refresh command or disconnect.

#### Scenario: Full FDN win

- **GIVEN** the PDN is in symbol play
- **WHEN** round-success is received
- **THEN** the PDN shows matched celebration UI

#### Scenario: New symbol period

- **GIVEN** the PDN is in matched celebration
- **WHEN** the refresh command is received
- **THEN** the PDN returns to symbol play for the new period

### Requirement: Symbol match SHALL keep puzzle play independent of Wi-Fi uploads

While players are actively connected and playing symbol match on the FDN, the FDN SHALL keep the peer-to-peer radio enabled on the fixed peer-to-peer channel (`6`) and SHALL NOT interrupt active symbol play to switch into Wi-Fi station mode.

#### Scenario: PDN connects during play

- **GIVEN** the FDN is in selection or play
- **WHEN** a provisioned PDN connects on an FDN upstream jack
- **THEN** the peer-to-peer radio remains enabled for jack detection, background match/crash collection, and symbol wireless messages without interrupting symbol play for a Wi-Fi upload
