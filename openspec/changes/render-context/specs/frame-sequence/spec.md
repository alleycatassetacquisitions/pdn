## ADDED Requirements

### Requirement: FrameSequence is the transmission envelope for ordered multi-frame experiences
`FrameSequence` SHALL be defined in `lib/core` as an ordered list of `InterfaceFrame` objects and a `CompletionPolicy`. A single-frame experience is a `FrameSequence` of length 1. All MC pushes and FDN-to-PDN experience launches SHALL use `FrameSequence` as the envelope, not a bare `InterfaceFrame`.

#### Scenario: Single-frame sequence
- **WHEN** a `FrameSequence` with one `InterfaceFrame` is received
- **THEN** the consuming state applies that frame and immediately evaluates the `CompletionPolicy`

#### Scenario: Multi-frame sequence
- **WHEN** a `FrameSequence` with three `InterfaceFrame` objects is received
- **THEN** the consuming state applies frame[0] on mount and advances to subsequent frames based on its own advancement logic

### Requirement: CompletionPolicy on FrameSequence determines behaviour after the last frame
`FrameSequence` SHALL carry a `CompletionPolicy` with two values: `RETURN_TO_PRIOR` and `HOLD_LAST_FRAME`. The consuming state SHALL evaluate this policy when the last frame has been processed.

#### Scenario: RETURN_TO_PRIOR policy
- **WHEN** the consuming state processes the last frame of a `FrameSequence` with `CompletionPolicy::RETURN_TO_PRIOR`
- **THEN** the state transitions back to the prior state or state machine that was active before the sequence began

#### Scenario: HOLD_LAST_FRAME policy
- **WHEN** the consuming state processes the last frame of a `FrameSequence` with `CompletionPolicy::HOLD_LAST_FRAME`
- **THEN** the last frame remains applied indefinitely until an explicit transition or new frame replaces it

### Requirement: The consuming state owns advancement — there is no default navigation behaviour
A `FrameSequence` SHALL NOT define any default button-press-to-advance behaviour. The state consuming the sequence is solely responsible for deciding when to advance. Frames without an `InputConfig` do not implicitly bind any button interaction.

#### Scenario: Frame without InputConfig does not bind any buttons
- **WHEN** a frame with a null `InputConfig` is applied by the render engine
- **THEN** no button callbacks are registered; the consuming state wires its own navigation callbacks if advancement is desired

#### Scenario: Frame with InputConfig — consuming state uses command result to advance
- **WHEN** a frame's `InputConfig` callback fires and the consuming state receives the resulting command
- **THEN** the state decides whether to advance to the next frame, stay, or complete the sequence based on its own logic

### Requirement: FrameSequence current index is owned by the consuming state
The consuming state SHALL track the current frame index within the active `FrameSequence`. On advancement, it SHALL call `renderEngine->apply(frames[currentIndex])` with the next frame.

#### Scenario: Consuming state advances through a sequence
- **WHEN** the consuming state increments `currentIndex` and applies the next frame
- **THEN** the render engine deregisters previous input callbacks, applies the new frame's modules, and the device reflects the new frame's display, LED, and input state
