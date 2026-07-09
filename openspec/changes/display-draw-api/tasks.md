## 1. Define Types in lib/core

- [ ] 1.1 Add `DISPLAY_INSTRUCTION_CAPACITY` and `DISPLAY_TEXT_CAPACITY` constexpr constants to an appropriate header in `lib/core/include`
- [ ] 1.2 Define `Anchor` enum class (`LEADING`, `CENTER`, `TRAILING`) in `lib/core/include/device/drivers/draw-instruction.hpp`
- [ ] 1.3 Define `DrawType` enum class (`IMAGE`, `TEXT`, `GLYPH`) in `draw-instruction.hpp`
- [ ] 1.4 Define `ImagePayload` struct (`Image image`, `int x`, `int y`) in `draw-instruction.hpp`
- [ ] 1.5 Define `TextPayload` struct (`FontMode font`, `char text[DISPLAY_TEXT_CAPACITY]`, `int x`, `int y`, `Anchor hAnchor`, `Anchor vAnchor`) with `LEADING` defaults in `draw-instruction.hpp`
- [ ] 1.6 Define `GlyphPayload` struct (`const char* unicode`, `int x`, `int y`) in `draw-instruction.hpp`
- [ ] 1.7 Define `DrawInstruction` struct with `DrawType type` and a `union` of the three payloads in `draw-instruction.hpp`

## 2. Update DisplayModule

- [ ] 2.1 Replace `DisplayModule { text?: string, icon?: ResourceId }` with `DisplayModule { std::array<DrawInstruction, DISPLAY_INSTRUCTION_CAPACITY> instructions; uint8_t count; }` in `lib/core/include`
- [ ] 2.2 Add a default constructor to `DisplayModule` that initialises `count = 0`
- [ ] 2.3 Add an `addInstruction(DrawInstruction)` helper method to `DisplayModule` that asserts `count < DISPLAY_INSTRUCTION_CAPACITY` before appending and increments `count`

## 3. Update render-context Artifacts

- [ ] 3.1 Update `openspec/changes/render-context/specs/interface-frame/spec.md` — replace the `DisplayModule { text?, icon? }` requirement with a reference to `draw-instruction` capability
- [ ] 3.2 Update `openspec/changes/render-context/migration-sketch.md` — replace the `display.text = "Wins\n..."` pattern with `DisplayModule` built from `DrawInstruction` list in `updateStatsDisplay()`

## 4. Verify

- [ ] 4.1 Confirm `draw-instruction.hpp` compiles cleanly on the ESP32 toolchain (no RTTI, no exceptions)
- [ ] 4.2 Confirm the union members are all trivially constructible (no `std::string` in payloads)
- [ ] 4.3 Confirm `DisplayModule` is a plain value type — no custom destructor, copyable by value
