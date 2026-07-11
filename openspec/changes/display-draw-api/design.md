## Context

The `Display` driver interface (`lib/core/include/device/drivers/display.hpp`) wraps u8g2 as an imperative API: `drawText(str, x, y)`, `drawImage(img)`, `setGlyphMode(FontMode)`, `renderGlyph(unicode, x, y)`. Every state that renders anything — `Idle::renderStats`, `Duel::onStateMounted`, etc. — sequences these calls directly, hardcoding pixel coordinates and font mode switches.

The `render-context` change introduces `InterfaceFrame` and `DisplayModule` as the declarative, driver-free description of what the display should show. Its current spec defines `DisplayModule` as `{ text?: string, icon?: ResourceId }`. That formulation is insufficient: `renderStats` alone draws one image, two text elements at distinct coordinates with distinct font modes, and a glyph variant — none of which maps to a single text string and icon.

This change defines the draw instruction model that makes `DisplayModule` expressively complete. It is a compile-time dependency of the `render-context` implementation.

**Constraints:**
- C++17 embedded target — no RTTI, limited heap, no exceptions, no `std::variant` overhead if avoidable
- Raw `new`/`delete` ownership model — no smart pointers
- `Display` driver interface is not changed — `PdnRenderEngine::applyDisplay` bridges instructions to driver calls
- `FontMode` enum already exists in `display.hpp` and is the right abstraction for font selection

## Goals / Non-Goals

**Goals:**
- Define `DrawInstruction` as the typed unit of display composition (image, text, glyph)
- Define `Anchor` enum to express how a text element relates to its coordinate — additive semantic over u8g2's raw pixel API
- Update `DisplayModule` to carry a fixed-capacity `DrawInstruction` list
- Keep the model representable as plain C++ structs with no heap allocation

**Non-Goals:**
- Modifying the `Display` driver interface or u8g2 usage
- Implementing `PdnRenderEngine::applyDisplay` (that is `render-context` work)
- Layout regions, bounding boxes, or multi-column layout
- Animated or dynamic display content (covered by `LedModule` / animation system)
- Font loading or font resource management

## Decisions

### 1. Tagged union over polymorphic base

**Decision:** `DrawInstruction` is a struct with a `DrawType` enum tag and a `union` of `ImagePayload`, `TextPayload`, and `GlyphPayload`.

**Rationale:** A polymorphic base with `virtual apply()` would require a vtable per instruction and heap allocation for each element in the list. A tagged union is zero-overhead, stack-allocatable, and does not require RTTI. The set of instruction types is closed and known at compile time, making a polymorphic open type unnecessary.

**Alternative considered:** `std::variant<ImagePayload, TextPayload, GlyphPayload>` — available in C++17, but carries implementation overhead and exception-path code on some toolchains. The tagged union is more predictable on ESP32.

### 2. Anchor enum: always a coordinate, anchoring is separate

**Decision:** `TextPayload` always carries an explicit `(x, y)` coordinate. `hAnchor` and `vAnchor` (`Anchor` enum values) describe how the text element relates to that coordinate: `LEADING` means the coordinate is the start edge, `CENTER` means the coordinate is the midpoint, `TRAILING` means the coordinate is the end edge. Default is `LEADING` on both axes, which matches u8g2's existing behaviour.

**Rationale:** The original formulation `TextAlignment(alignmentEnum, optional int)` with `ABSOLUTE` as an enum variant conflates two concerns: where to place the element and how the coordinate is interpreted. Making `ABSOLUTE` a special case of an alignment enum is a design smell — `ABSOLUTE` breaks the contract of the other variants (alignment needs no coordinate; absolute needs one). Separating the coordinate (always present) from the anchor (always present, defaults to `LEADING`) eliminates the optional field and the special case entirely.

**Alternative considered:** Axis-specific alignment enums (`HAlign { LEFT, CENTER, RIGHT }`, `VAlign { TOP, MIDDLE, BOTTOM }`) — more explicit about direction but not axis-agnostic; `LEADING`/`CENTER`/`TRAILING` work correctly for both axes without implying screen directionality.

### 3. Fixed-capacity array on DisplayModule

**Decision:** `DisplayModule` carries `std::array<DrawInstruction, DISPLAY_INSTRUCTION_CAPACITY> instructions` and a `uint8_t count`. `DISPLAY_INSTRUCTION_CAPACITY` is a compile-time constant (initial value: 8). Instructions beyond `count` are ignored.

**Rationale:** `std::vector` requires heap allocation and is inappropriate for an embedded value type that is copied as part of `InterfaceFrame` deltas. A fixed array is a plain value type: it can be stack-allocated, copied by value, and has no destructor complexity. Capacity of 8 covers all known states with headroom (`renderStats` max: image + 2 texts = 3 instructions).

**Alternative considered:** A pointer + count to a caller-owned array — avoids copying but introduces lifetime complexity when `DisplayModule` is stored in `pendingDelta`.

### 4. GlyphPayload is distinct from TextPayload

**Decision:** `GlyphPayload { const char* unicode; int x; int y; }` is its own payload type rather than a variant of `TextPayload`.

**Rationale:** Glyphs use `renderGlyph(unicode, x, y)` — a different driver call with different semantics (`FontMode::SYMBOL_GLYPH`, a Unicode code point string, no text wrapping). Reusing `TextPayload` would require either a sentinel `FontMode` value or a nullable `unicode` field. A separate payload type keeps both cleaner and makes the driver translation unambiguous.

## Risks / Trade-offs

- **Capacity overflow:** If a state constructs more than `DISPLAY_INSTRUCTION_CAPACITY` instructions, extras are silently dropped. Mitigation: add a debug assertion (`assert(count < DISPLAY_INSTRUCTION_CAPACITY)`) in the helper that appends instructions.

- **render-context dependency:** The `render-context` migration sketch and `DisplayModule` spec in that change document the old `text?/icon?` formulation. Those artifacts need updating to reference `DrawInstruction`. This is a documentation-only update — no code path in `render-context` has shipped yet.

- **Union of non-trivial types:** `std::string` in `TextPayload` cannot be placed in a C++ `union` without custom construction/destruction. Mitigation: use `const char*` for text content in `TextPayload` (pointer to a string literal or caller-owned buffer), consistent with the existing `Display::drawText(const char*)` signature.

## Open Questions

1. **`const char*` lifetime in TextPayload** — if a state builds a `DrawInstruction` from a `std::to_string()` result and stores it in `pendingDelta` across ticks, the `const char*` dangling is a risk. Should `TextPayload` carry a fixed-length char buffer instead (e.g. `char text[32]`), or is the caller responsible for ensuring the string outlives the frame?

2. **GlyphPayload anchoring** — the current `renderGlyph(unicode, x, y)` call in `renderStats` passes the top-left corner explicitly. Should `GlyphPayload` also carry `hAnchor`/`vAnchor` for symmetry, or is absolute top-left positioning sufficient for the known glyph use cases?
