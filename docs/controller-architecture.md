# FDN ↔ PDN Controller Architecture

**Status:** Proposal — open for team discussion
**Branch context:** [`fdn-experience`](https://github.com/alleycatassetacquisitions/pdn/tree/fdn-experience)

---

## Background

The `fdn-experience` branch introduces the first end-to-end FDN game experience: a PDN physically connects to an FDN, exchanges a symbol to pair, and then acts as a wireless game controller while the FDN runs the game interface. The branch proves the concept works. This document proposes a more principled architecture for the communication layer and the controller state model before we build further on top of it.

---

## Goals

1. **Standardized communication format** — a shared, versioned command vocabulary that both FDN and PDN firmware build against.
2. **Reusable framework** — a set of base classes on each device that reduce the boilerplate required to implement a new controller experience, and make correct behavior the default.
3. **FDN as source of truth** — the FDN drives game state. The PDN is a display terminal and input peripheral. The PDN does not make game logic decisions locally.

---

## Assumptions

1. A PDN enters a controller state only when connected to an FDN (detected via `ConnectState::isConnected()` and `connectedDevice.deviceType == DeviceType::FDN`).
2. If a PDN disconnects from an FDN, all active controller states abort and the PDN returns to `Quickdraw`.
3. Each concrete controller state responds to a subset of commands sent by the FDN. Unregistered commands are silently dropped.
4. Pairing (symbol exchange via `SymbolWirelessManager`) is a *precondition* to entering the controller flow, not part of it.
5. Disconnect detection is handled by polling `ConnectState::isPersistentlyDisconnected()` — no explicit disconnect command is required.

---

## Proposed Architecture

### Communication Protocol

All FDN→PDN and PDN→FDN controller messages are unified under a single **command** abstraction. The shared command vocabulary lives in `lib/core` so both firmware targets build against the same definitions.

```
lib/core/include/controller/
    controller-command-types.hpp   ← shared command IDs and packet structs
```

**FDN→PDN channel** (`kFdnCommand`) — the FDN sends commands to drive PDN state.

| Command | Purpose |
|---|---|
| `GAME_SELECT` | Session setup — tells PDN which game mode to enter |
| `COMMAND_SET` | Sends an ordered list of selectable actions to the PDN; PDN cycles through them with the top button |
| `GAME_EVENT` | Game state update — the FDN pushes state the PDN should display (scores, prompts, outcomes) |
| *(extensible)* | Additional commands added as game experiences are built |

A `COMMAND_SET` packet contains an ordered list of command options. Each option has an ID and a display label. The PDN renders the current selection and cycles through the list on each top button press. The FDN can push an updated `COMMAND_SET` at any time — the PDN replaces its list and resets the selection index to 0.

```
CommandSet {
    options: [ { id: CMD_A, label: "OPTION A" },
               { id: CMD_B, label: "OPTION B" },
               { id: CMD_C, label: "OPTION C" } ]
}
```

**PDN→FDN channel** (`kPdnInput`) — the PDN sends semantic input events to the FDN.

| Command | Purpose |
|---|---|
| `SELECTION_CONFIRMED` | Bottom button pressed — carries the ID of the currently selected command |
| *(extensible)* | Additional input types (long press, hold, etc.) |

The PDN never sends raw button identifiers. It sends the command that was selected when the player confirmed. The FDN is fully decoupled from the PDN's physical button layout.

> **Note on the old packet types:** `kGameSelect`, `kControllerCommand`, and `kGameResponse` collapse into the two channels above. The echo-confirmation pattern (FDN echoing the button ID back to PDN) is removed. The FDN sends `GAME_EVENT` or `COMMAND_SET` when game state changes — not as a reflexive acknowledgment of input.

> **Pattern recognition:** This interaction model is a direct generalization of the existing `SymbolState` behavior — the FDN sends a symbol (the command set), the PDN cycles through symbols with the top button, and confirms a selection with the bottom button. The new architecture formalizes and extends that pattern.

---

### PDN Side: `ControllerState` Base Class

`ControllerState` is a base class that concrete PDN controller states extend. It is **not** a state machine — it is a base `ConnectState<PDN>` subclass that provides controller infrastructure. Any state machine can contain `ControllerState` subclasses.

#### What the base class owns

**Disconnect policy**
`onStateLoop` polls `isPersistentlyDisconnected()`. If the FDN connection is lost, the base class sets a transition flag that drives an app-level jump back to `Quickdraw`. Concrete states do not implement disconnect logic.

**Button callbacks**
On mount, the base class registers both button callbacks. On dismount, it removes them. Concrete states never touch button registration. Button behavior is fixed and universal:

- **Top button** — advances the selection index through the current command set, wrapping around. Renders the newly selected option's label.
- **Bottom button** — sends `SELECTION_CONFIRMED(currentCommand.id)` to the FDN.

Concrete states have no button API to override. Button behavior is fully owned by the base class.

**Command set management**
The base class holds the active `CommandSet` received from the FDN. When a new `COMMAND_SET` command arrives, the base class replaces the current set and resets the selection index to 0 — even if the PDN is mid-selection. The base class owns rendering the current selection label; concrete states do not need to handle `COMMAND_SET` in their registry.

**Command registry and dispatch**
The base class holds a `map<CommandId, std::function<void(CommandPayload)>>`. Incoming FDN commands are dispatched through this registry. Unregistered command IDs are dropped with a log warning. States call `registerCommand(id, callback)` — typically in their constructor — to declare what they handle. `COMMAND_SET` is always handled by the base class and never reaches the concrete state's registry.

**Lifecycle hooks**
The base class implements `onStateMounted` / `onStateLoop` / `onStateDismounted` and exposes protected hooks:

```cpp
virtual void onControllerMounted(PDN* pdn);
virtual void onControllerLoop(PDN* pdn);
virtual void onControllerDismounted(PDN* pdn);
```

Concrete states override these hooks, not the raw lifecycle methods.

#### API sketch (not implementation)

```cpp
class ControllerState : public ConnectState<PDN> {
public:
    bool isPrimaryRequired() override { return true; }
    bool isAuxRequired()     override { return true; }

protected:
    void registerCommand(CommandId id, std::function<void(CommandPayload)> callback);

    virtual void onControllerMounted(PDN* pdn)    {}
    virtual void onControllerLoop(PDN* pdn)        {}
    virtual void onControllerDismounted(PDN* pdn)  {}

private:
    void onStateMounted(PDN* pdn) final;
    void onStateLoop(PDN* pdn) final;
    void onStateDismounted(PDN* pdn) final;

    // Always handled by base — never reaches concrete state registry
    void onCommandSetReceived(CommandSet commandSet, PDN* pdn);
    void renderCurrentSelection(PDN* pdn);

    CommandSet activeCommandSet;
    int selectionIndex = 0;
    std::map<CommandId, std::function<void(CommandPayload)>> commandRegistry;
    bool transitionToQuickdrawState = false;
};
```

---

### PDN Side: Routing State

`ControllerRoutingState` is a **provided** concrete `ControllerState` subclass. Developers do not write this — they include it. It is always the entry point of the controller experience.

**Behavior:**
- Displays a neutral "connecting..." screen on mount.
- Registers `GAME_SELECT` in its command registry.
- On receipt of `GAME_SELECT(gameId)`, stores the game ID and sets a transition flag.
- The containing state machine wires transitions from the routing state to concrete game states, keyed on game ID.

**Result:** The FDN fully controls which game the PDN enters. The PDN never assumes a game context. Adding a new game requires registering a new transition from `ControllerRoutingState` — no changes to the routing state itself.

```
[FDN connects]
      │
      ▼
ControllerRoutingState
      │  receives GAME_SELECT(CONTROLLER_1)
      ├──────────────────────────────────► Controller1State
      │  receives GAME_SELECT(FUTURE_GAME)
      ├──────────────────────────────────► FutureGameState
      │  FDN disconnects
      └──────────────────────────────────► Quickdraw (app jump)
```

---

### PDN Side: Concrete Controller States

A concrete controller state extends `ControllerState` and registers only the FDN commands it cares about. It does not implement any button logic.

#### Minimal example

```cpp
class Controller1State : public ControllerState {
public:
    Controller1State() {
        registerCommand(CommandId::GAME_EVENT, [this](CommandPayload p) {
            onGameEvent(p);
        });
    }

protected:
    void onControllerMounted(PDN* pdn) override {
        // render initial state — command set cycling UI is handled by base class
    }

private:
    void onGameEvent(CommandPayload payload) {
        // update display based on FDN-pushed game state
    }
};
```

The FDN drives the available actions by sending `COMMAND_SET` at any point during the state's lifetime. The base class replaces the cycling list and re-renders automatically. The concrete state only handles semantic events (`GAME_EVENT`, etc.) — it is never involved in the selection UI.

---

### PDN Side: State Machine Assembly

The concrete controller states live in an app-specific state machine. Pairing (symbol exchange) is handled separately — the `Controller` app starts with `ControllerRoutingState`, not with symbol states.

The pairing flow routes to the `Controller` app after `SymbolMatchedState` in `Quickdraw` receives a `GAME_SELECT` signal. This is the seam between the pairing infrastructure and the controller infrastructure.

```
Quickdraw::Idle
  └── FDN connected
        ▼
  Quickdraw::SymbolState     (ConnectState — uses SymbolWirelessManager)
        └── symbol matched
              ▼
  Quickdraw::SymbolMatchedState
        └── GAME_SELECT received → app jump
              ▼
  Controller app (TypedStateMachine<PDN>)
    ├── ControllerRoutingState  [entry point]
    │     └── GAME_SELECT(CONTROLLER_1) → Controller1State
    └── Controller1State
          └── FDN disconnects → Quickdraw (app jump)
```

> **Open question:** Does `SymbolMatchedState` listen for `GAME_SELECT` and fire the app jump, or does the `Controller` app launch unconditionally and the routing state handles `GAME_SELECT`? The former keeps `Quickdraw` cleaner but makes `SymbolMatchedState` aware of the controller protocol.

---

### FDN Side: Open

The FDN needs a symmetrical base class for game states that drive controller sessions. The shape is not yet defined. It will likely include:

- A `sendCommand(CommandId, CommandPayload, SerialIdentifier port)` API that pushes game state to one or all connected PDNs.
- A registered callback for receiving PDN input events per port.
- A disconnect detection mechanism (checking RDC port state).

This is deferred — we will design the FDN-side base class once the PDN-side model is stable.

---

### Wireless Layer: Per-Port Routing

The current `ControllerWirelessManager` holds a single `macPeer[6]`. This will be replaced with a `map<SerialIdentifier, MacAddress>` — the same pattern used for callback registration. Send methods will accept a `SerialIdentifier` and look up the MAC internally. `setMacPeer()` becomes an internal implementation detail, not part of the public API.

This is planned as part of the upcoming communications architecture rewrite.

---

## Open Questions for Discussion

These decisions are not yet made. Input from the team is needed.

### 1. ~~Button payload mechanism~~ — Resolved

The `COMMAND_SET` / `SELECTION_CONFIRMED` model supersedes this question. The base class fully owns both buttons. The PDN never sends raw button identifiers — it sends the ID of the command selected when the player confirmed. No virtual payload methods are needed.

### 2. FDN-driven display

The `COMMAND_SET` pattern partially answers this — the command cycling UI is entirely FDN-driven (the FDN provides labels, the base class renders them). The remaining question is how `GAME_EVENT` payloads interact with the PDN display.

**Option A — `GAME_EVENT` includes display data:**
The payload carries rendered display content (text, glyph IDs, layout hints). The base `ControllerState` handles a standard render pass before dispatching to the registry. The PDN makes no display decisions for `GAME_EVENT`.

**Option B — `GAME_EVENT` carries semantic data:**
The payload carries game-meaningful data (a score value, an outcome ID, a prompt string). Concrete states interpret it and call their own render logic. The PDN's concrete state drives its display based on received game state.

Option A is the more fully FDN-authoritative model and keeps concrete states simpler. Option B is more conventional C++ and gives concrete states more flexibility. The key practical question: is the FDN better positioned to know how to render on a PDN, or should the PDN retain that knowledge about itself?

### 3. SymbolState / ControllerState boundary

Does `SymbolState` extend `ControllerState` (symbol exchange is the first controller state), or does `ControllerState` presuppose completed pairing?

The proposed architecture above treats pairing as a precondition — `SymbolState` stays as a plain `ConnectState<PDN>` subclass, and `ControllerState` only applies to states that operate after the FDN–PDN session is established. If the symbol exchange mechanic changes significantly in the future (e.g., the FDN assigns symbols rather than the PDN selecting them), this boundary should be revisited.

---

## Future Considerations

### Daisy-chained devices

When a HEAD device in a daisy chain connects to an FDN, the chain's downstream devices may also need to participate in the controller session. The command registry pattern should not preclude forwarding — a registered command handler could forward a received command downstream rather than consuming it. The `consume vs. propagate` distinction should be documented as a known extension point when we formalize the command registry API. No implementation is needed now.

---

## Summary of Proposed Changes

| Area | Current | Proposed |
|---|---|---|
| Controller pairing | Inside `Controller` state machine | Stays in `Quickdraw` (`SymbolState` / `SymbolMatchedState`) |
| Controller entry | `Controller` state machine starts with `SymbolState` | `Controller` app starts with `ControllerRoutingState` |
| Game routing | `kGameSelect` received by `SymbolMatchedState` | `ControllerRoutingState` handles `GAME_SELECT` and routes |
| Button callbacks | Registered per-state manually | Always owned by `ControllerState` base class — not overridable |
| Button behavior | Per-state custom logic | Top = cycle command set; bottom = confirm selection — universal |
| Command dispatch | Per-state callback registration on wireless manager | Centralized command registry on `ControllerState` |
| FDN→PDN packets | `kGameSelect` + `kGameResponse` (echo) | Unified `kFdnCommand` with `GAME_SELECT`, `COMMAND_SET`, `GAME_EVENT` |
| PDN→FDN packets | `kControllerCommand` (raw button IDs) | `kPdnInput` with `SELECTION_CONFIRMED(commandId)` — semantic, not physical |
| Available actions | Hardcoded per-state | FDN-pushed `COMMAND_SET`, updateable live mid-state |
| MAC routing | Single mutable `macPeer` | Per-port MAC map (with comms rewrite) |
| Disconnect | Per-state flag + per-state `clearCallback()` | Handled by `ControllerState` base class transparently |
