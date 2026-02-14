#pragma once

#include "state/state.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include <string>
#include <vector>
#include <cstdint>

/*
 * KonamiFdnDisplay — Display logic for the mysterious 8th FDN node.
 *
 * This state manages a cryptic UI with 13 symbol positions that cycle
 * through hieroglyphic symbols. As the player collects Konami buttons
 * from the 7 minigames, positions are revealed to show the actual
 * Konami Code sequence.
 *
 * Visual Modes:
 * 1. Idle (no player connected):
 *    - All 13 positions cycle through random unicode symbols
 *    - Each position has randomized cycle speed (200-800ms)
 *
 * 2. Partial Reveal (player has some buttons):
 *    - Locked positions (buttons earned) → show actual symbols (↑ ↓ ← → B A ★)
 *    - Unlocked positions → continue cycling
 *    - Progress text: "X of 7 buttons collected"
 *
 * 3. Full Reveal (all 7 buttons collected):
 *    - All 13 positions revealed showing the Konami Code
 *    - "K O N A M I" text appears
 *    - "[DOWN to begin]" prompt
 *    - DOWN button transitions to KonamiCodeEntry (state 32)
 *
 * Button-to-Position Mapping (MUST match exactly):
 * Signal Echo (0) → positions 0, 1 (UP UP)
 * Ghost Runner (1) → positions 2, 3 (DOWN DOWN)
 * Spike Vector (2) → positions 4, 6 (LEFT LEFT)
 * Firewall Decrypt (3) → positions 5, 7 (RIGHT RIGHT)
 * Cipher Path (4) → positions 8, 10 (B B)
 * Exploit Sequencer (5) → positions 9, 11 (A A)
 * Breach Defense (6) → position 12 (START)
 */

enum class KonamiFdnStateId : int {
    KONAMI_FDN_DISPLAY = 30
};

class KonamiFdnDisplay : public State {
public:
    explicit KonamiFdnDisplay(Player* player);
    ~KonamiFdnDisplay() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToKonamiCodeEntry();

private:
    Player* player;
    SimpleTimer cycleTimer;
    SimpleTimer renderTimer;
    bool transitionToKonamiCodeEntryState = false;
    bool displayIsDirty = true;

    // 13 positions in the Konami Code sequence
    static constexpr int POSITION_COUNT = 13;
    static constexpr int KONAMI_CODE_ENTRY_STATE_ID = 32;

    // Symbol cycling state
    struct PositionState {
        int currentSymbolIndex = 0;
        int cycleIntervalMs = 400;
        unsigned long lastCycleTime = 0;
    };

    PositionState positions[POSITION_COUNT];

    // The actual Konami Code: UP UP DOWN DOWN LEFT LEFT RIGHT RIGHT B B A A START
    static constexpr KonamiButton KONAMI_SEQUENCE[POSITION_COUNT] = {
        KonamiButton::UP,     // 0
        KonamiButton::UP,     // 1
        KonamiButton::DOWN,   // 2
        KonamiButton::DOWN,   // 3
        KonamiButton::LEFT,   // 4
        KonamiButton::RIGHT,  // 5
        KonamiButton::LEFT,   // 6
        KonamiButton::RIGHT,  // 7
        KonamiButton::B,      // 8
        KonamiButton::A,      // 9
        KonamiButton::B,      // 10
        KonamiButton::A,      // 11
        KonamiButton::START   // 12
    };

    // Unicode symbol pool for cycling (when positions are locked)
    static constexpr const char* CRYPTIC_SYMBOLS[] = {
        "☥", "♆", "☾", "⚶", "♁", "⊛", "☽", "◈",
        "♃", "⚷", "✧", "◇", "△", "⚡", "☄", "⚛"
    };
    static constexpr int SYMBOL_COUNT = 16;

    // Game type to position mapping
    struct ButtonMapping {
        GameType gameType;
        int positions[2];  // Most games unlock 2 positions, last one unlocks 1
        int positionCount;
    };

    static constexpr ButtonMapping BUTTON_MAPPINGS[7] = {
        { GameType::SIGNAL_ECHO,       {0, 1},   2 },  // UP UP
        { GameType::GHOST_RUNNER,      {2, 3},   2 },  // DOWN DOWN
        { GameType::SPIKE_VECTOR,      {4, 6},   2 },  // LEFT LEFT
        { GameType::FIREWALL_DECRYPT,  {5, 7},   2 },  // RIGHT RIGHT
        { GameType::CIPHER_PATH,       {8, 10},  2 },  // B B
        { GameType::EXPLOIT_SEQUENCER, {9, 11},  2 },  // A A
        { GameType::BREACH_DEFENSE,    {12, -1}, 1 }   // START
    };

    void initializePositions();
    void updateCyclingSymbols(Device* PDN);
    void renderDisplay(Device* PDN);
    void renderIdleMode(Device* PDN);
    void renderPartialReveal(Device* PDN);
    void renderFullReveal(Device* PDN);

    bool isPositionUnlocked(int position) const;
    int getUnlockedButtonCount() const;
    const char* getSymbolForPosition(int position) const;
    int getRandomSymbolIndex() const;
};
