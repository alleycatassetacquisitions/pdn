## ADDED Requirements

### Requirement: Anchor defines axis-agnostic text anchoring to a coordinate
`Anchor` SHALL be an `enum class` with three values: `LEADING`, `CENTER`, `TRAILING`. `LEADING` means the coordinate is the start edge of the element on that axis. `CENTER` means the coordinate is the midpoint. `TRAILING` means the coordinate is the end edge. `Anchor` SHALL apply independently to horizontal and vertical axes.

#### Scenario: LEADING anchor — coordinate is start edge
- **WHEN** a `TextPayload` has `hAnchor = Anchor::LEADING` and `x = 74`
- **THEN** the render engine draws the text with its left edge at x=74, matching u8g2's default behaviour

#### Scenario: CENTER anchor — coordinate is midpoint
- **WHEN** a `TextPayload` has `hAnchor = Anchor::CENTER` and `x = 96`
- **THEN** the render engine draws the text centered around x=96, computing `xDraw = 96 - (textWidth / 2)`

#### Scenario: TRAILING anchor — coordinate is end edge
- **WHEN** a `TextPayload` has `hAnchor = Anchor::TRAILING` and `x = 128`
- **THEN** the render engine draws the text right-aligned to x=128, computing `xDraw = 128 - textWidth`

---

### Requirement: TextPayload carries font, text, coordinate, and anchor
`TextPayload` SHALL contain: `FontMode font`, `char text[DISPLAY_TEXT_CAPACITY]` (null-terminated, fixed buffer), `int x`, `int y`, `Anchor hAnchor` (default `LEADING`), `Anchor vAnchor` (default `LEADING`). `x` and `y` are always absolute pixel coordinates; their semantic meaning on each axis is determined by `hAnchor` and `vAnchor`.

#### Scenario: TextPayload with default anchors behaves identically to direct drawText call
- **WHEN** a `TextPayload` is constructed with `font`, `text`, `x`, `y` and no explicit anchors
- **THEN** `hAnchor` defaults to `Anchor::LEADING` and `vAnchor` defaults to `Anchor::LEADING`, producing the same output as `display->setGlyphMode(font)->drawText(text, x, y)`

#### Scenario: TextPayload text is safely bounded
- **WHEN** text longer than `DISPLAY_TEXT_CAPACITY - 1` characters is assigned to a `TextPayload`
- **THEN** the text is truncated to fit the fixed buffer with a null terminator preserved

---

### Requirement: ImagePayload carries image resource and absolute coordinate
`ImagePayload` SHALL contain `Image image` and `int x`, `int y`. The coordinate is always the top-left origin of the image. No anchoring is applied to images.

#### Scenario: ImagePayload applied at specified coordinate
- **WHEN** an `ImagePayload` with a valid `Image` and `x = 0`, `y = 0` is applied
- **THEN** the render engine calls `display->drawImage(image, 0, 0)`

#### Scenario: ImagePayload with default coordinate
- **WHEN** an `ImagePayload` is constructed with only an `Image` and no explicit coordinate
- **THEN** `x` and `y` default to `0`

---

### Requirement: GlyphPayload carries a Unicode glyph string and absolute coordinate
`GlyphPayload` SHALL contain `const char* unicode` and `int x`, `int y`. The render engine SHALL use `FontMode::SYMBOL_GLYPH` and call `renderGlyph(unicode, x, y)`. The coordinate is the top-left origin of the glyph bounding box.

#### Scenario: GlyphPayload applied
- **WHEN** a `GlyphPayload` with a valid Unicode string and coordinate is applied
- **THEN** the render engine calls `display->setGlyphMode(FontMode::SYMBOL_GLYPH)->renderGlyph(unicode, x, y)`

---

### Requirement: DrawInstruction is a tagged union of the three payload types
`DrawInstruction` SHALL be a struct containing `DrawType type` (enum class: `IMAGE`, `TEXT`, `GLYPH`) and a `union` of `ImagePayload image`, `TextPayload text`, and `GlyphPayload glyph`. Only the member corresponding to `type` is valid; accessing other members is undefined behaviour.

#### Scenario: DrawInstruction with TEXT type carries a valid TextPayload
- **WHEN** a `DrawInstruction` is constructed with `DrawType::TEXT`
- **THEN** `instruction.text` is the valid member and `instruction.image` and `instruction.glyph` are not accessed

#### Scenario: DrawInstruction with IMAGE type carries a valid ImagePayload
- **WHEN** a `DrawInstruction` is constructed with `DrawType::IMAGE`
- **THEN** `instruction.image` is the valid member

#### Scenario: DrawInstruction with GLYPH type carries a valid GlyphPayload
- **WHEN** a `DrawInstruction` is constructed with `DrawType::GLYPH`
- **THEN** `instruction.glyph` is the valid member

---

### Requirement: DisplayModule carries a fixed-capacity DrawInstruction list
`DisplayModule` SHALL contain `std::array<DrawInstruction, DISPLAY_INSTRUCTION_CAPACITY> instructions` and `uint8_t count`. `DISPLAY_INSTRUCTION_CAPACITY` SHALL be a `constexpr` constant (initial value: `8`). `count` is the number of valid instructions in `instructions[0..count-1]`. Instructions at indices `>= count` SHALL be ignored. A default-constructed `DisplayModule` SHALL have `count = 0`.

#### Scenario: DisplayModule default state is empty
- **WHEN** a `DisplayModule` is default-constructed
- **THEN** `count` is `0` and no draw calls are made when it is applied

#### Scenario: DisplayModule with multiple instructions
- **WHEN** a `DisplayModule` has `count = 3` with one `IMAGE`, one `TEXT` (small font, label), and one `TEXT` (large font, value)
- **THEN** the render engine executes all three instructions in order: image first, then label, then value

#### Scenario: Instructions are executed in order
- **WHEN** a `DisplayModule` contains instructions at indices 0, 1, 2
- **THEN** the render engine applies them in ascending index order (0, then 1, then 2)

#### Scenario: Appending beyond capacity is bounds-checked
- **WHEN** a helper attempts to append a `DrawInstruction` to a `DisplayModule` with `count == DISPLAY_INSTRUCTION_CAPACITY`
- **THEN** a debug assertion fires and the instruction is not written, leaving `count` unchanged
