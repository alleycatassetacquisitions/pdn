# UI Design Option A: Clean Minimal

**Design Philosophy**: Maximum white space, simple geometric icons, thin lines, ultra-compact status elements

## Design System

### Typography
- **Body**: `u8g2_font_tenfatguys_tr` (10px monospace, clean)
- **Small**: `u8g2_font_tenthinnerguys_t_all` (8px, status bar)
- **Large**: `u8g2_font_helvB14_tr` (14px, headings)

### Icons
- **Small (8x8)**: Open Iconic 1x series
  - `u8g2_font_open_iconic_check_1x_t` - checkmarks, X marks
  - `u8g2_font_open_iconic_arrow_1x_t` - arrows, indicators
  - `u8g2_font_open_iconic_embedded_1x_t` - tech symbols
  - `u8g2_font_open_iconic_gui_1x_t` - UI elements
- **Large (16x16)**: Open Iconic 2x series
  - `u8g2_font_open_iconic_play_2x_t` - media, gameplay
  - `u8g2_font_open_iconic_check_2x_t` - win/lose states
- **Status**: `u8g2_font_siji_t_6x10` (6x10, tiny indicators)

### Layout Grid
- Screen: 128x64 pixels
- Margins: 4px minimum
- Status bar: 8px height (top)
- Content area: 128x52 pixels (remaining)

### Visual Elements
- **Frames**: 1px lines, no fills
- **Borders**: Thin, subtle
- **Spacing**: 2-4px between elements
- **Alignment**: Centered for focus content, left-aligned for lists

---

## Screen Designs (ASCII Mockups)

### 1. Game Intro Screen
```
┌──────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│                        ▶ GHOST RUNNER                          │
│                                                                │
│                           DIFFICULTY                           │
│                           [  EASY  ]                           │
│                                                                │
│                       Press to start race                      │
│                                                                │
│                                                                │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

**Layout**:
- Icon (16x16 play arrow): x=56, y=12
- Game name (14px): centered at y=28
- Difficulty label (8px): centered at y=38
- Difficulty box (10px text in frame): centered at y=48
- Instructions (8px): centered at y=58

**Fonts**:
- Play icon: `u8g2_font_open_iconic_play_2x_t` glyph 80 (▶)
- Game name: `u8g2_font_helvB14_tr`
- Difficulty: `u8g2_font_tenthinnerguys_t_all`
- Button text: `u8g2_font_tenfatguys_tr`

**Code snippet**:
```cpp
display->invalidateScreen();
display->setFont(u8g2_font_open_iconic_play_2x_t);
display->drawGlyph(56, 16, 80); // Play icon
display->setFont(u8g2_font_helvB14_tr);
display->drawStr(30, 28, "GHOST RUNNER");
display->setFont(u8g2_font_tenthinnerguys_t_all);
display->drawStr(48, 38, "DIFFICULTY");
display->drawFrame(44, 40, 40, 12); // Border
display->setFont(u8g2_font_tenfatguys_tr);
display->drawStr(52, 50, hardMode ? "HARD" : "EASY");
display->setFont(u8g2_font_tenthinnerguys_t_all);
display->drawStr(28, 60, "Press to start race");
display->render();
```

---

### 2. Win Screen
```
┌──────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│                                                                │
│                              ✓                                 │
│                                                                │
│                           VICTORY!                             │
│                          Score: 450                            │
│                                                                │
│                                                                │
│                      PRESS TO CONTINUE                         │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

**Layout**:
- Checkmark icon (16x16): centered at x=56, y=20
- "VICTORY!" (14px): centered at y=36
- Score (10px): centered at y=46
- Continue prompt (8px): centered at y=60

**Fonts**:
- Check icon: `u8g2_font_open_iconic_check_2x_t` glyph 64 (✓)
- Victory text: `u8g2_font_helvB14_tr`
- Score: `u8g2_font_tenfatguys_tr`
- Prompt: `u8g2_font_tenthinnerguys_t_all`

**Code snippet**:
```cpp
display->invalidateScreen();
display->setFont(u8g2_font_open_iconic_check_2x_t);
display->drawGlyph(56, 24, 64); // Check icon
display->setFont(u8g2_font_helvB14_tr);
display->drawStr(38, 38, "VICTORY!");
display->setFont(u8g2_font_tenfatguys_tr);
char scoreText[32];
snprintf(scoreText, sizeof(scoreText), "Score: %d", score);
int scoreWidth = display->getStrWidth(scoreText);
display->drawStr((128 - scoreWidth) / 2, 50, scoreText);
display->setFont(u8g2_font_tenthinnerguys_t_all);
display->drawStr(24, 60, "PRESS TO CONTINUE");
display->render();
```

---

### 3. Lose Screen
```
┌──────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│                                                                │
│                              ✗                                 │
│                                                                │
│                            FAILED                              │
│                        Attempts: 2/3                           │
│                                                                │
│                                                                │
│                       Press to retry                           │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

**Layout**:
- X mark icon (16x16): centered at x=56, y=20
- "FAILED" (14px): centered at y=36
- Attempts (10px): centered at y=46
- Retry prompt (8px): centered at y=60

**Fonts**:
- X icon: `u8g2_font_open_iconic_check_2x_t` glyph 66 (✗)
- Failed text: `u8g2_font_helvB14_tr`
- Attempts: `u8g2_font_tenfatguys_tr`
- Prompt: `u8g2_font_tenthinnerguys_t_all`

**Code snippet**:
```cpp
display->invalidateScreen();
display->setFont(u8g2_font_open_iconic_check_2x_t);
display->drawGlyph(56, 24, 66); // X icon
display->setFont(u8g2_font_helvB14_tr);
display->drawStr(42, 38, "FAILED");
display->setFont(u8g2_font_tenfatguys_tr);
char attemptsText[32];
if (attemptsRemaining > 0) {
    snprintf(attemptsText, sizeof(attemptsText), "Attempts: %d/3", 3 - attemptsRemaining);
} else {
    snprintf(attemptsText, sizeof(attemptsText), "GAME OVER");
}
int attWidth = display->getStrWidth(attemptsText);
display->drawStr((128 - attWidth) / 2, 50, attemptsText);
display->setFont(u8g2_font_tenthinnerguys_t_all);
display->drawStr(36, 60, attemptsRemaining > 0 ? "Press to retry" : "");
display->render();
```

---

### 4. HUD During Gameplay
```
┌──────────────────────────────────────────────────────────────┐
│♥♥♥ 450  [████████░░░░░░░░░░░░░░░░░░░░]  E                    │
├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─┤
│                                                                │
│                        GAME CONTENT                            │
│                         AREA HERE                              │
│                                                                │
│                                                                │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

**Layout (8px height)**:
- Lives (hearts): x=2, y=7 (3 hearts, 6px wide each)
- Score: x=24, y=7 (10px font)
- Timer bar: x=54, y=2 (60px wide, 5px tall)
- Difficulty: x=118, y=7 (E/H, 8px font)
- Separator line: y=9 (1px)

**Fonts**:
- Hearts: `u8g2_font_siji_t_6x10` glyphs for hearts
- Score: `u8g2_font_tenthinnerguys_t_all`
- Difficulty: `u8g2_font_tenthinnerguys_t_all`

**Code snippet**:
```cpp
// HUD at top 8px
display->setFont(u8g2_font_siji_t_6x10);
for (int i = 0; i < lives; i++) {
    display->drawStr(2 + i * 7, 7, "♥");
}
display->setFont(u8g2_font_tenthinnerguys_t_all);
char scoreText[16];
snprintf(scoreText, sizeof(scoreText), "%d", score);
display->drawStr(24, 7, scoreText);
// Timer bar (filled portion)
int barFilled = (timeRemaining * 60) / maxTime;
display->drawBox(54, 2, barFilled, 5);
display->drawFrame(54, 2, 60, 5);
// Difficulty indicator
display->drawStr(118, 7, hardMode ? "H" : "E");
// Separator
display->drawHLine(0, 9, 128);
```

---

### 5. Konami Progress Screen
```
┌──────────────────────────────────────────────────────────────┐
│                                                                │
│                       KONAMI PROGRESS                          │
│                                                                │
│                                                                │
│          ▣   ▢   ▢   ▣   ▣   ▢   ▣                            │
│          1   2   3   4   5   6   7                            │
│                                                                │
│                           5 / 7                                │
│                                                                │
│                                                                │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

**Layout**:
- Title (14px): centered at y=16
- Button icons (8x8): y=32, evenly spaced across 112px width
  - Start x=16, spacing=16px between centers
- Button numbers (8px): y=44, below each icon
- Progress count (10px): centered at y=56

**Fonts**:
- Title: `u8g2_font_helvB14_tr`
- Icons: `u8g2_font_open_iconic_gui_1x_t` (filled/empty boxes)
- Numbers: `u8g2_font_tenthinnerguys_t_all`
- Count: `u8g2_font_tenfatguys_tr`

**Code snippet**:
```cpp
display->invalidateScreen();
display->setFont(u8g2_font_helvB14_tr);
display->drawStr(22, 18, "KONAMI PROGRESS");
display->setFont(u8g2_font_open_iconic_gui_1x_t);
for (int i = 0; i < 7; i++) {
    int x = 16 + i * 16;
    if (player->hasKonamiButton(i)) {
        display->drawGlyph(x, 36, 73); // Filled box
    } else {
        display->drawGlyph(x, 36, 74); // Empty box
    }
}
display->setFont(u8g2_font_tenthinnerguys_t_all);
for (int i = 0; i < 7; i++) {
    char num[2] = {'1' + i, '\0'};
    display->drawStr(18 + i * 16, 46, num);
}
display->setFont(u8g2_font_tenfatguys_tr);
char progressText[16];
int collected = player->getKonamiCount();
snprintf(progressText, sizeof(progressText), "%d / 7", collected);
int progWidth = display->getStrWidth(progressText);
display->drawStr((128 - progWidth) / 2, 56, progressText);
display->render();
```

---

### 6. Idle/Menu Screen
```
┌──────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│                         PLAYER-ID                              │
│                         ──────────                             │
│                                                                │
│                           ●  ◐                                 │
│                        HUNTER  WIFI                            │
│                                                                │
│                                                                │
│                    Waiting for opponent...                     │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

**Layout**:
- Player ID (14px): centered at y=20
- Underline: y=22, centered (80px width)
- Allegiance icon (8x8): x=56, y=36
- WiFi icon (8x8): x=72, y=36
- Allegiance label (8px): x=44, y=48
- Connection label (8px): x=76, y=48
- Status text (10px): centered at y=60

**Fonts**:
- Player ID: `u8g2_font_helvB14_tr`
- Icons: `u8g2_font_open_iconic_gui_1x_t`, `u8g2_font_open_iconic_embedded_1x_t`
- Labels: `u8g2_font_tenthinnerguys_t_all`
- Status: `u8g2_font_tenfatguys_tr`

**Code snippet**:
```cpp
display->invalidateScreen();
display->setFont(u8g2_font_helvB14_tr);
const char* playerId = player->getId().c_str();
int idWidth = display->getStrWidth(playerId);
display->drawStr((128 - idWidth) / 2, 22, playerId);
display->drawHLine((128 - 80) / 2, 24, 80);
// Allegiance icon
display->setFont(u8g2_font_open_iconic_gui_1x_t);
display->drawGlyph(56, 40, player->isHunter() ? 69 : 70);
// WiFi icon
display->setFont(u8g2_font_open_iconic_embedded_1x_t);
display->drawGlyph(72, 40, isConnected ? 77 : 78);
// Labels
display->setFont(u8g2_font_tenthinnerguys_t_all);
display->drawStr(44, 50, player->isHunter() ? "HUNTER" : "BOUNTY");
display->drawStr(76, 50, "WIFI");
// Status
display->setFont(u8g2_font_tenfatguys_tr);
display->drawStr(18, 60, "Waiting for opponent...");
display->render();
```

---

## Icon Reference

### Open Iconic Check 2x (u8g2_font_open_iconic_check_2x_t)
- 64: ✓ (checkmark)
- 66: ✗ (X mark)
- 67: ✓ in circle
- 71: ✗ in circle

### Open Iconic Play 2x (u8g2_font_open_iconic_play_2x_t)
- 80: ▶ (play)
- 81: ⏸ (pause)
- 82: ⏹ (stop)
- 83: ⏪ (rewind)

### Open Iconic GUI 1x (u8g2_font_open_iconic_gui_1x_t)
- 69: ● (filled circle)
- 70: ○ (empty circle)
- 73: ▣ (filled box)
- 74: ▢ (empty box)
- 75: ◆ (filled diamond)
- 76: ◇ (empty diamond)

### Open Iconic Embedded 1x (u8g2_font_open_iconic_embedded_1x_t)
- 77: WiFi connected
- 78: WiFi disconnected
- 80: Chip/processor
- 81: Signal

### Open Iconic Arrow 1x (u8g2_font_open_iconic_arrow_1x_t)
- 64: ← (left)
- 65: → (right)
- 66: ↑ (up)
- 67: ↓ (down)
- 73: ↻ (refresh/retry)

---

## Implementation Notes

### Display Flow
1. Call `invalidateScreen()` to clear buffer
2. Set font with `setFont()`
3. Draw elements with `drawStr()`, `drawGlyph()`, `drawBox()`, `drawFrame()`
4. Call `render()` to push buffer to screen

### Best Practices
- **Measure text first**: Use `getStrWidth()` for centering
- **Layer bottom-up**: Draw backgrounds before text
- **Use frames sparingly**: Only for emphasis
- **Consistent spacing**: 2-4px between elements
- **Status bar separation**: Always draw line after HUD

### Performance
- Buffer updates are fast (~16ms)
- Keep HUD updates minimal (only changed values)
- Cache computed positions for repeated elements

---

## Rationale

### Why Clean Minimal?
1. **Readability**: Maximum contrast, no visual clutter
2. **Speed**: Fast to implement, easy to test
3. **Consistency**: Clear hierarchy, predictable layout
4. **Accessibility**: Simple shapes, large targets

### Design Decisions
- **8px status bar**: Leaves 52px for content (81% of screen)
- **16x16 win/lose icons**: Large enough to see at a glance
- **Centered text**: Focus attention, easy to scan
- **Thin borders**: Define areas without dominating
- **Monospace fonts**: Consistent spacing, technical aesthetic

### Trade-offs
- **Pros**: Clean, fast, readable, easy to maintain
- **Cons**: Less personality than custom graphics
- **When to use**: Production builds, quick iteration, accessibility focus

---

## Integration with Existing Code

### Files to Modify
1. `include/game/quickdraw-resources.hpp` - Add font constants
2. `src/game/quickdraw-states/fdn-complete-state.cpp` - Win/lose screens
3. New file: `include/game/ui-helpers.hpp` - Reusable layout functions

### Example Helper Functions
```cpp
// Center text horizontally
void drawCenteredText(Display* display, const char* text, int y) {
    int width = display->getStrWidth(text);
    display->drawStr((128 - width) / 2, y, text);
}

// Draw HUD
void drawGameHUD(Display* display, int lives, int score, int timePercent, bool hardMode) {
    // Implementation from HUD design above
}

// Draw outcome screen
void drawOutcome(Display* display, bool won, int score, int attemptsRemaining) {
    // Implementation from win/lose designs above
}
```

---

## Testing

### CLI Simulator
```bash
pdncli 2
# In simulator:
display 0
```

### Visual Verification
- [ ] All text is readable at 128x64
- [ ] Icons are recognizable at size
- [ ] Spacing feels balanced
- [ ] HUD doesn't obscure content
- [ ] Borders are visible but subtle

### Integration Tests
- [ ] Win screen renders correctly
- [ ] Lose screen shows attempts remaining
- [ ] HUD updates smoothly during gameplay
- [ ] Konami progress displays all 7 slots
- [ ] Idle screen shows connection status

---

*Design A: Clean Minimal — Maximum clarity, minimum complexity*
