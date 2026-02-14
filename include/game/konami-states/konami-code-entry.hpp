#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include <vector>

/*
 * KonamiCodeEntry - The 8th FDN: Konami Code Entry State
 *
 * Player must enter the classic Konami code sequence using the 7 unlocked buttons:
 * UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A B A START (13 inputs total)
 *
 * Controls:
 * - Primary button: cycles through unlocked buttons
 * - Secondary button: commits selected button to sequence
 *
 * On correct input: advance, provide visual/haptic confirmation
 * On incorrect input: reset to position 0, provide error feedback
 * On completion (all 13 correct): transition to KonamiCodeAccepted
 * Timeout (30 seconds of inactivity): transition to GameOverReturnIdle
 *
 * OLED Display:
 * "ENTER THE CODE"
 * "↑ ↑ ↓ ↓ _ _ _ _"
 * "4 of 13"
 */
class KonamiCodeEntry : public State {
public:
    explicit KonamiCodeEntry(Player* player);
    ~KonamiCodeEntry() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToAccepted();
    bool transitionToGameOver();

private:
    Player* player;
    SimpleTimer inactivityTimer;
    static constexpr int INACTIVITY_TIMEOUT_MS = 30000;
    bool transitionToAcceptedState = false;
    bool transitionToGameOverState = false;

    // The 13-input Konami code sequence
    static constexpr int SEQUENCE_LENGTH = 13;
    const KonamiButton targetSequence[SEQUENCE_LENGTH] = {
        KonamiButton::UP, KonamiButton::UP,
        KonamiButton::DOWN, KonamiButton::DOWN,
        KonamiButton::LEFT, KonamiButton::RIGHT,
        KonamiButton::LEFT, KonamiButton::RIGHT,
        KonamiButton::B, KonamiButton::A,
        KonamiButton::B, KonamiButton::A,
        KonamiButton::START
    };

    // Current state
    int currentPosition = 0;
    int selectedButtonIndex = 0;
    std::vector<KonamiButton> unlockedButtons;
    bool displayIsDirty = true;
    bool pendingButtonPress = false;

    // Helper methods
    void buildUnlockedButtons();
    void renderDisplay(Device* PDN);
    void processButtonPress(Device* PDN);
    void provideCorrectFeedback(Device* PDN);
    void provideIncorrectFeedback(Device* PDN);
    void resetSequence(Device* PDN);
    const char* getButtonSymbol(KonamiButton button);
};
