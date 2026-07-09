## ADDED Requirements

### Requirement: LedModule carries a base layer and optional overlays
`LedModule` SHALL contain a base layer (either `AnimationDescriptor { IAnimation*, AnimationConfig }` or a static `LEDState`) and an optional `LedOverlay[]`. The base layer defines the full LED strip state. Overlays target specific LEDs or zones and are composited on top of the base layer.

#### Scenario: LedModule with animation base and no overlays
- **WHEN** a `LedModule` with an `AnimationDescriptor` base and an empty overlay list is applied
- **THEN** the render engine drives the animation each tick and writes each frame directly to the light driver

#### Scenario: LedModule with static LEDState base
- **WHEN** a `LedModule` with a static `LEDState` base and no overlays is applied
- **THEN** the render engine writes the static state to the light driver on apply and does not produce new frames on tick

### Requirement: LedOverlay targets specific LEDs or a LightIdentifier zone with a colour and optional TTL
`LedOverlay` SHALL contain: a target (either a `LightIdentifier` zone or specific LED index list), a `LEDColor colour`, a `uint8_t brightness`, and an optional `uint32_t ttlMs`. A null `ttlMs` means the overlay persists until explicitly removed or the frame is cleared.

#### Scenario: Overlay targets transmit light
- **WHEN** a `LedOverlay` targeting `LightIdentifier::TRANSMIT_LIGHT` is composited
- **THEN** the transmit LED is written with the overlay colour regardless of what the base animation produced for that LED

#### Scenario: Overlay targets specific grip LED indices
- **WHEN** a `LedOverlay` targeting LED indices [0, 1] on `LightIdentifier::GRIP_LIGHTS` is composited
- **THEN** only those two grip LEDs are overridden; all other grip LEDs render their base animation values

### Requirement: Render engine composites base layer and overlays on each tick
On each `tick()` call, the render engine SHALL: (1) generate the base frame by calling `animation->animate()` (or use the static `LEDState`), (2) remove any overlays whose `ttlMs` has elapsed, (3) apply remaining overlays to the base frame by overwriting targeted LED values, (4) write the composited `LEDState` to the `LightStrip` driver.

#### Scenario: Overlay composited over running animation
- **WHEN** an animation is running and an overlay is active on the transmit light
- **THEN** each tick produces the animation's frame for all LEDs except the transmit light, which shows the overlay colour; all other LEDs animate normally

#### Scenario: Overlay expires mid-animation
- **WHEN** a `LedOverlay` with `ttlMs = 200` has been active for 200ms while an animation runs
- **THEN** the overlay is removed before the next composite; the targeted LED resumes its animation value on the next tick

### Requirement: Overlays can be added to the active LedModule without replacing the base layer
The render engine SHALL support adding a `LedOverlay` to the currently active `LedModule` without requiring a full frame re-apply. This enables states to add short-duration effects (transmit flash, MC easter egg) without interrupting the current animation.

#### Scenario: State adds transmit flash overlay mid-animation
- **WHEN** a state adds a `LedOverlay` targeting the transmit light with `ttlMs = 100` while an idle animation is running
- **THEN** the transmit light flashes for 100ms and the idle animation continues uninterrupted on all other LEDs

### Requirement: clearFrame() stops the active animation and removes all overlays
When `clearFrame()` is called, the render engine SHALL stop the active animation (calling `animation->stop()`), clear the overlay list, and write a cleared `LEDState` to the `LightStrip`.

#### Scenario: clearFrame clears animation and overlays
- **WHEN** `clearFrame()` is called with an animation and two overlays active
- **THEN** the animation is stopped, both overlays are removed, and all LEDs are set to off
