# Symbol match

## Purpose

Specify symbol match end-to-end: **FDN-hosted** dual-jack play, **offline** operation, **rotating symbols**, **dual-jack win**, **per-FDN fixed codeword** on success for **30 seconds**, then **reset**. PDN behavior is limited to jack detection and wireless symbol exchange; no server integration.

## Requirements

### Requirement: FDN SHALL boot directly into symbol match

The FDN firmware SHALL start in the symbol match application without requiring idle, hacking, or main menu applications first.

#### Scenario: Cold boot

- **GIVEN** a powered FDN running symbol-match firmware
- **WHEN** startup completes
- **THEN** the symbol match state machine is active
- **AND** the user does not see hacking or unrelated app UI

### Requirement: Symbol match SHALL run without a backend

The experience SHALL not depend on HTTP, cloud APIs, or server-backed authorization. Symbol play, codeword reveal, and reset SHALL complete with no network calls.

#### Scenario: No server connectivity

- **GIVEN** the FDN has no usable server connection
- **WHEN** players play symbol match
- **THEN** behavior is unchanged from the connected-server case
- **AND** no upload or sync states run

### Requirement: FDN SHALL assign independent symbols per jack

The FDN SHALL hold one symbol per input jack from a shared **five-symbol** set. On each refresh, each new symbol SHALL differ from **both** symbols from the previous period.

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

### Requirement: PDN SHALL enter symbol play when FDN is on a jack

When the PDN detects an FDN on an input jack, it SHALL enter symbol play state and remain there while the FDN connection holds.

#### Scenario: FDN plugged in

- **GIVEN** a registered PDN idle or gameplay shell that supports FDN detection
- **WHEN** an FDN is detected on an input jack
- **THEN** the PDN enters symbol play for that session

#### Scenario: FDN unplugged

- **GIVEN** the PDN is in symbol play
- **WHEN** the FDN is no longer detected on the jack
- **THEN** the PDN leaves symbol play for idle (or equivalent home state)

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

### Requirement: Symbol match SHALL exclude legacy FDN apps

Symbol match firmware SHALL NOT require hacking, idle authorization, hack storage, main-menu routing after win, or HTTP upload states.

#### Scenario: PDN connects during play

- **GIVEN** the FDN is in selection or play
- **WHEN** a PDN connects
- **THEN** only jack detection and symbol wireless messages apply
- **AND** hack history or server auth does not alter the flow
