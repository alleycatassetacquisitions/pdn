#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <string>
#include <vector>

/*
 * KonamiMetaGame States — Visual redesign for the 8th FDN ("The Cipher Wall")
 *
 * This file contains the 3 new states for the KonamiMetaGame system:
 * 1. KonamiScanSequence — Boot-sequence scan animation before routing
 * 2. KonamiMasteryComplete — Final "Cowboy" message for full mastery
 * 3. KonamiHardModeProgress — Shows X/7 powers stolen when player returns
 *
 * State Routing:
 * IF FdnGameType == KONAMI_CODE:
 *     IF hasAllColorPalettes() → ScanSequence("MASTERY DETECTED") → MasteryComplete
 *     ELIF hasHardMode() && hasAllKonamiButtons() → ScanSequence("ACCESS VERIFIED") → HardModeProgress
 *     ELIF hasAllKonamiButtons() → ScanSequence("ACCESS VERIFIED") → FullReveal → CodeEntry
 *     ELSE → ScanSequence("INSUFFICIENT ACCESS") → PartialReveal ("NOT READY")
 */

/*
 * KonamiScanSequence — Boot-sequence scan animation (1.5-2 seconds)
 *
 * This state performs a cryptic boot-sequence scan before routing to the
 * appropriate next state based on player progress.
 *
 * Animation sequence:
 * 1. Typewriter text: "> SCANNING..." at 30ms/char
 * 2. Scramble-decode: "> ID: [hex]" (scramble random chars → hex, 400ms)
 * 3. Noise-fill progress bar: 50% random noise fill (30x28, 80x8)
 * 4. STATUS line: scramble-decode to result text
 * 5. LED pulse: red (insufficient), green (verified), gold (mastery)
 *
 * Routes to:
 * - KonamiMasteryComplete (if hasAllColorPalettes)
 * - KonamiHardModeProgress (if hasHardMode && hasAllKonamiButtons)
 * - KonamiFdnDisplay FULL_REVEAL (if hasAllKonamiButtons)
 * - KonamiFdnDisplay PARTIAL_REVEAL (otherwise)
 */
class KonamiScanSequence : public State {
public:
    explicit KonamiScanSequence(Player* player);
    ~KonamiScanSequence() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToMasteryComplete();
    bool transitionToHardModeProgress();
    bool transitionToFdnDisplay();

private:
    Player* player;
    SimpleTimer animationTimer;
    SimpleTimer scanTimer;
    bool transitionToMasteryCompleteState = false;
    bool transitionToHardModeProgressState = false;
    bool transitionToFdnDisplayState = false;
    bool displayIsDirty = true;

    // Animation state
    enum class ScanPhase {
        TYPEWRITER_SCANNING,
        SCRAMBLE_ID,
        NOISE_BAR,
        SCRAMBLE_STATUS,
        COMPLETE
    };

    ScanPhase currentPhase = ScanPhase::TYPEWRITER_SCANNING;
    int animationStep = 0;
    std::string statusMessage;
    std::string hexId;
    static constexpr int TOTAL_SCAN_DURATION_MS = 2000;
    static constexpr int TYPEWRITER_CHAR_MS = 30;
    static constexpr int SCRAMBLE_DURATION_MS = 400;

    // Helper methods
    void determineStatusMessage();
    void renderScanAnimation(Device* PDN);
    void typewriterReveal(Device* PDN, const std::string& text, int x, int y, int charsRevealed);
    void scrambleDecode(Device* PDN, const std::string& target, int x, int y, float progress);
    void noiseFillBar(Device* PDN, int x, int y, int w, int h, float progress);
    void updateLedPulse(Device* PDN);
    char getRandomChar() const;
    bool hasAllColorPalettes() const;
    bool hasAllKonamiButtons() const;
    bool hasHardMode() const;
};

/*
 * KonamiMasteryComplete — Final "Cowboy" message for full mastery
 *
 * Shown when player has beaten all 7 FDNs on hard mode AND has collected
 * all 7 color palettes. This is the ultimate achievement message.
 *
 * Display sequence:
 * 1. 1s black screen silence
 * 2. "KONAMI HAS SEEN YOU." typewriter at 80ms/char with micro-haptic per char
 * 3. "MAY FORTUNE FAVOR YOU, COWBOY" scramble-decode
 * 4. Rainbow bar at bottom (7 colored segments)
 * 5. Full brightness rainbow LED sweep
 * 6. NO TIMEOUT — stays until disconnect
 *
 * LED pattern: Rainbow sweep across all game colors
 * Haptic: Micro-pulse per character during typewriter
 *
 * Returns to Idle on ANY button press (celebration complete)
 */
class KonamiMasteryComplete : public State {
public:
    explicit KonamiMasteryComplete(Player* player);
    ~KonamiMasteryComplete() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToIdle();

private:
    Player* player;
    SimpleTimer animationTimer;
    SimpleTimer ledTimer;
    bool transitionToIdleState = false;
    bool displayIsDirty = true;

    // Animation state
    enum class MasteryPhase {
        BLACK_SCREEN,
        TYPEWRITER_KONAMI,
        SCRAMBLE_COWBOY,
        RAINBOW_SWEEP,
        COMPLETE
    };

    MasteryPhase currentPhase = MasteryPhase::BLACK_SCREEN;
    int animationStep = 0;
    int typewriterChars = 0;
    static constexpr int BLACK_SCREEN_MS = 1000;
    static constexpr int TYPEWRITER_CHAR_MS = 80;
    static constexpr int SCRAMBLE_DURATION_MS = 800;

    // Helper methods
    void renderMasteryDisplay(Device* PDN);
    void typewriterReveal(Device* PDN, const std::string& text, int x, int y, int charsRevealed);
    void scrambleDecode(Device* PDN, const std::string& target, int x, int y, float progress);
    void drawRainbowBar(Device* PDN);
    void updateRainbowLeds(Device* PDN);
    char getRandomChar() const;
};

/*
 * KonamiHardModeProgress — Shows X/7 powers stolen when player returns
 *
 * Shown when player has Konami's Boon active (all 7 buttons + hard mode unlocked)
 * and returns to the FDN. Displays their progress stealing powers from the 7 FDNs.
 *
 * Display:
 * - "BOON ACTIVE" header
 * - "POWERS STOLEN: X / 7" with noise-fill progress bar
 * - "STEAL THEM ALL" or "RETURN TO THE FDNs" prompt
 *
 * LED pattern: Gold pulse (Konami color)
 * Haptic: Single pulse on mount
 *
 * Returns to Idle after 5 seconds or on button press
 */
class KonamiHardModeProgress : public State {
public:
    explicit KonamiHardModeProgress(Player* player);
    ~KonamiHardModeProgress() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToIdle();

private:
    Player* player;
    SimpleTimer displayTimer;
    SimpleTimer ledTimer;
    static constexpr int DISPLAY_DURATION_MS = 5000;
    bool transitionToIdleState = false;
    bool displayIsDirty = true;
    int stolenPowers = 0;

    // Helper methods
    void renderProgressDisplay(Device* PDN);
    void noiseFillBar(Device* PDN, int x, int y, int w, int h, float progress);
    void updateLedPulse(Device* PDN);
    int countStolenPowers() const;
};
