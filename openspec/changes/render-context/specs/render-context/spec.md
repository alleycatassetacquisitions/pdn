<!-- STUB: This spec captures the concept and known requirements. Scenarios and full detail to be completed before implementation. -->

## ADDED Requirements

### Requirement: RenderContext is the unified composable PDN state object
`RenderContext` SHALL be defined in `lib/core` as the top-level object that describes the complete state of a PDN's display and input system at any given moment. It SHALL contain a `DisplayModule` (always present), an `InputModule` (equivalent to current `GamepadConfig`/`InputConfig`), and a `bool showInstructions` flag. Future modules (`LedConfig`, `HapticConfig`) SHALL be addable without breaking existing contexts that omit them.

#### Scenario: RenderContext applied to PDN
- **WHEN** a PDN receives a `RenderContext` (via `kFdnCommand` or `kMcEvent`)
- **THEN** it atomically replaces its active display state, input bindings, and instruction screen visibility with the new context

### Requirement: DisplayModule carries pre-formatted display data
`DisplayModule` SHALL carry `text?: string` and `icon?: ResourceId` — the same fields as the current `GameEventPayload`. Both fields SHALL be nullable. The FDN (or MC) formats display content; the PDN owns layout and asset lookup.

#### Scenario: RenderContext with display content only
- **WHEN** a `RenderContext` is received with a populated `DisplayModule` and an empty `InputModule`
- **THEN** the PDN updates its display without rewiring button bindings

### Requirement: InputModule carries button bindings — equivalent to current GamepadConfig InputConfig
`InputModule` SHALL define button-to-command mappings using the same structure as the current `InputConfig` in `GamepadConfig`. `CommandSet` SHALL remain a top-level field on `RenderContext` (not nested in `InputModule`) so it can be updated independently.

#### Scenario: RenderContext replaces active button bindings
- **WHEN** a `RenderContext` with a populated `InputModule` is received
- **THEN** `ControllerState` deregisters all current button callbacks and rewires per the new `InputModule` — identical atomicity to current `GAMEPAD_CONFIG` handling

### Requirement: RenderContext supports partial updates via module-scoped commands
A source MAY send a partial update that affects only one module of the active `RenderContext` (e.g. display-only update, command-set-only update) without replacing the full context. The current `GAME_EVENT` and standalone `COMMAND_SET` commands are instances of this pattern.

#### Scenario: Display-only update received mid-state
- **WHEN** a display-only `RenderContext` update arrives (populated `DisplayModule`, null `InputModule`)
- **THEN** the PDN updates its display and leaves button bindings unchanged

### Requirement: LedModule supports layered composition — animation base with individual LED overrides
The `LedModule` SHALL support a layered compositing model. A base layer MAY run an animation across all or a subset of LEDs. One or more overlay layers MAY target individual LEDs or LED zones independently, running concurrently with the base animation. The `RenderEngine` SHALL composite layers in order (base first, overlays on top) on each animation frame, so individual LED overrides visually sit above the animation without interrupting it.

> **Note (stub):** The specific layer data model (e.g. `AnimationDescriptor` base + `vector<LedOverride>` overlays), zone addressing, blend modes, and overlay TTL semantics are to be designed. Known use cases driving this requirement:
> - Transmit light flashes during data send while an idle animation runs on grip/display LEDs
> - Game event highlights a specific LED pair while background animation continues
> - MC-pushed overlay (e.g. easter egg colour flash on grip LEDs) while local animation plays

#### Scenario: Individual LED fires while animation is running
- **WHEN** an overlay targeting a single LED is applied while a looping animation occupies the full LED strip
- **THEN** the targeted LED displays the overlay colour on each frame while all other LEDs continue to animate normally

#### Scenario: Overlay expires and animation resumes full control
- **WHEN** an overlay with a defined duration reaches its end
- **THEN** the previously overridden LED is returned to full animation control without interrupting the animation state

### Requirement: Unknown future modules are silently ignored
When a PDN firmware version receives a `RenderContext` containing modules it does not recognise (e.g. `LedConfig` from a newer MC version), it SHALL ignore the unknown fields and apply the modules it does understand.

#### Scenario: Newer MC sends RenderContext with LedConfig to older PDN
- **WHEN** a PDN that does not implement `LedConfig` receives a `RenderContext` containing one
- **THEN** the PDN applies display and input modules normally; the `LedConfig` field is ignored without error
