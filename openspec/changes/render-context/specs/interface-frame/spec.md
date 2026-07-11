## ADDED Requirements

### Requirement: InterfaceFrame is the composable unit describing complete device peripheral and input state
`InterfaceFrame` SHALL be defined in `lib/core` with four independently nullable modules: `DisplayModule? display`, `LedModule? leds`, `InputConfig? input`, and `HapticModule? haptics`. A null module means "do not change this aspect of the current device state." `InterfaceFrame` replaces `GamepadConfig` as the canonical type for describing device state.

#### Scenario: Full frame applied to device
- **WHEN** an `InterfaceFrame` with all modules populated is applied
- **THEN** the render engine updates display, LEDs, input bindings, and haptics in a single atomic operation

#### Scenario: Partial frame applied — display-only update
- **WHEN** an `InterfaceFrame` with only `DisplayModule` populated (all others null) is applied
- **THEN** the render engine updates the display and leaves LED animation, button bindings, and haptics unchanged

### Requirement: DisplayModule carries nullable text and icon
`DisplayModule` SHALL contain `text?: string` and `icon?: ResourceId`. Both fields SHALL be independently nullable. The FDN or MC pre-formats display content; the device owns layout, text wrapping, and asset lookup.

#### Scenario: DisplayModule with text only
- **WHEN** a `DisplayModule` with `text = "Draw!"` and `icon = null` is applied
- **THEN** the display renders the text without an icon

#### Scenario: DisplayModule with icon only
- **WHEN** a `DisplayModule` with `text = null` and a valid `ResourceId` is applied
- **THEN** the display renders the icon without any text

### Requirement: InputConfig contains CommandSet and button bindings
`InputConfig` SHALL contain `CommandSet commandSet`, `bool cycleConfirmConfig`, `ButtonConfig primaryButton`, and `ButtonConfig secondaryButton`. `CommandSet` SHALL be a member of `InputConfig` — it is not a top-level field on `InterfaceFrame`.

#### Scenario: InputConfig applied
- **WHEN** an `InterfaceFrame` with a populated `InputConfig` is applied
- **THEN** the render engine atomically deregisters all existing button callbacks and registers the new ones from `ButtonConfig`

### Requirement: ButtonConfig carries CommandId for wire identity and a nullable Callback for runtime wiring
`ButtonConfig` SHALL contain `CommandId commandId` and `Callback callback` (nullable). `commandId` is always serialised on the wire. `callback` SHALL always be null on deserialise and SHALL be populated by the consuming state before the frame is passed to the render engine. The render engine wires non-null callbacks to the button driver and silently skips null ones.

#### Scenario: State populates callbacks before applying frame
- **WHEN** a state constructs an `InterfaceFrame` with an `InputConfig`
- **THEN** each bound `ButtonConfig` field has a non-null `callback` constructed by the state before the frame is handed to the render engine

#### Scenario: Frame received over network — callbacks are null
- **WHEN** an `InterfaceFrame` is deserialised from an ESP-NOW or MQTT payload
- **THEN** all `ButtonConfig.callback` fields are null; no callbacks are registered until the consuming state populates them

### Requirement: HapticModule is a defined placeholder for future implementation
`HapticModule` SHALL be defined as a struct in `lib/core`. The render engine SHALL accept it as a nullable module on `InterfaceFrame`. In v1 the render engine's `applyHaptics` implementation is a no-op. Adding haptic behaviour SHALL NOT require changes to `InterfaceFrame` or `RenderEngine`'s public interface.

#### Scenario: InterfaceFrame with HapticModule applied on v1 firmware
- **WHEN** an `InterfaceFrame` with a populated `HapticModule` is applied on a v1 device
- **THEN** the haptic module is silently ignored without affecting display, LED, or input application

### Requirement: Unknown modules are silently ignored
When a device receives an `InterfaceFrame` containing a module it does not implement, it SHALL ignore that module and apply all modules it does recognise without error.

#### Scenario: Newer firmware sends frame with unknown module to older device
- **WHEN** a device running older firmware receives an `InterfaceFrame` with an unrecognised module field
- **THEN** it applies the modules it recognises and discards the unknown field without logging an error
