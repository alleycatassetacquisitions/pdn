# Symbol match — design reference

Normative behavior is in [spec.md](./spec.md). This document helps implementation: state machine, protocol, and legacy code pointers.

## FDN state machine (target)

```
     ┌─────────────┐
     │  Selection  │  ~1s loading, refresh symbols, start 30m timer
     └──────┬──────┘
            ▼
     ┌─────────────┐     30m timer      ┌─────────────┐
     │ SymbolIdle  │ ─────────────────► │  Selection  │
     │   (play)    │                    └─────────────┘
     └──────┬──────┘
            │ both jacks win
            ▼
     ┌─────────────┐     30s            ┌─────────────┐
     │  Codeword   │ ─────────────────► │  Selection  │
     │   Success   │                    └─────────────┘
     └─────────────┘
```

**Remove from legacy FDN symbol-match app:** upload pending, connection detected, auth, unauthorized, transition to main menu on win, `HackedPlayersManager` / HTTP from match success, `PDN_CONNECT` handling inside symbol match, optional nearby-player RSSI attract.

**Rename / replace:** `MatchSuccess` → codeword success (30s hold, then Selection; no main menu).

## Configuration

Per-unit codeword at build or flash time, for example:

```cpp
constexpr const char* FDN_CODEWORD = "RAVEN";
```

Symbols: five IDs `SYMBOL_A` … `SYMBOL_E` with custom glyph font on display.

Timing constants (from legacy implementation):

| Constant | Value |
|----------|--------|
| Selection buffer | ~1000 ms |
| Symbol refresh period | 30 minutes |
| Codeword success | 30 seconds |
| Play UI blink / render | ~500 ms (PDN); FDN idle blink ~500 ms |

## Wireless — symbol match (ESP-NOW)

- **Packet type:** `kSymbolMatchCommand` (legacy value `13` in `peer-comms-types.hpp`)
- **Payload:** packed `{ int command; SymbolId symbolId; }`

| Command | Value | Typical direction | Meaning |
|---------|------:|-------------------|---------|
| `SEND_SYMBOL` | 0 | Both | Symbol value for this jack |
| `SYMBOL_MATCH_SUCCESS` | 1 | FDN → PDN | Both jacks won on FDN |
| `SYMBOLS_REFRESHED` | 2 | FDN → PDN | New symbol period; reset PDN UI |

Incoming packets are mapped to left vs right jack by matching sender MAC to peers registered on `INPUT_JACK` / `INPUT_JACK_SECONDARY`.

## PDN states (legacy names)

| State | Responsibility |
|-------|----------------|
| `SymbolState` | Blink/show player symbol; RX FDN symbol; button send when symbols align; RX success / refresh |
| `SymbolMatched` | Celebration blink; RX refresh → back to `SymbolState` |

FDN peer MAC: whichever input jack reports `DeviceType::FDN`.

## FDN play UI (legacy behavior to preserve)

- Left glyph ~x=24, right ~x=72; countdown `MM:SS` bottom.
- Unmatched side blinks; matched side white half + solid glyph.
- Side LEDs: blue recess/fin pattern per matched jack (only after FDN has sent symbol to that jack).

## Reference code (legacy)

| Area | Path (typical) |
|------|----------------|
| FDN app shell | `src/apps/symbol-match/symbol-match.cpp` |
| Play state | `symbol-match-states/symbol-idle-state.cpp` |
| Symbol manager | `symbol-manager.cpp` |
| Wireless | `src/wireless/symbol-wireless-manager.cpp` |
| PDN play | `src/game/quickdraw-states/symbol-state.cpp` |
| PDN matched | `symbol-matched-state.cpp` |

Do not port hacking, idle auth, or upload states into the symbol-match-only build.
