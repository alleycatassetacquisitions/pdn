## Why

The `Display` driver exposes an imperative, pixel-coordinate API (`drawText(str, x, y)`, `drawImage(img)`, `setGlyphMode(FontMode)`) inherited from u8g2. States like `Idle::renderStats` hardcode absolute pixel positions and manually sequence font-mode switches with no layout semantics. The `render-context` change introduces `DisplayModule` as the declarative display description carried by `InterfaceFrame`, but the current spec for it (`text?: string, icon?: ResourceId`) is too thin — it cannot express font differentiation, explicit per-element positioning, or the glyph-render case, and `render-context` cannot be fully implemented until this gap is resolved.

## What Changes

- Introduces a `DrawInstruction` tagged-union type replacing direct driver calls as the unit of display composition
- Introduces `Anchor` enum (`LEADING`, `CENTER`, `TRAILING`) — axis-agnostic anchoring of a text element to its coordinate, additive over what u8g2 provides
- Introduces `ImagePayload`, `TextPayload`, and `GlyphPayload` as the typed instruction payloads
- Replaces `DisplayModule { text?, icon? }` with `DisplayModule { instructions[], count }` — a fixed-capacity draw list
- The existing `Display` driver interface is unchanged — `PdnRenderEngine::applyDisplay` translates draw instructions into driver calls

## Capabilities

### New Capabilities

- `draw-instruction`: The draw instruction model — `DrawInstruction` union type, `DrawType` enum, `ImagePayload`, `TextPayload` (with `FontMode`, text, `x`, `y`, `hAnchor`, `vAnchor`), `GlyphPayload`, `Anchor` enum, and updated `DisplayModule` carrying a fixed-capacity instruction list

### Modified Capabilities

<!-- None — DisplayModule is introduced by the render-context change which is not yet implemented; the draw-instruction spec supplants the thin definition there -->

## Impact

- `lib/core`: new `draw-instruction.hpp` defining `Anchor`, `DrawType`, the payload structs, `DrawInstruction`, and updated `DisplayModule`
- `render-context` change: `DisplayModule` spec and migration sketch need updating to reference draw instructions instead of `text?/icon?`; this change is a dependency of `render-context` implementation
- `PdnRenderEngine::applyDisplay`: translates `DrawInstruction` list to `Display` driver calls — the only new driver-touching code required
- States (post-migration): `updateStatsDisplay()` and equivalents produce `DrawInstruction` lists rather than calling the driver directly
