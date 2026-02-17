#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include <vector>

// Forward declarations
class ProgressManager;

/*
 * ColorProfilePrompt — YES/NO prompt after winning hard mode.
 * Asks whether to equip the newly unlocked color palette.
 * YES: equips palette, saves progress, transitions to Idle.
 * NO: transitions to Idle, eligibility preserved for later.
 * Auto-dismiss after 10 seconds defaults to NO.
 */
class ColorProfilePrompt : public State {
public:
    explicit ColorProfilePrompt(Player* player, ProgressManager* progressManager);
    ~ColorProfilePrompt();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToIdle();

private:
    Player* player;
    ProgressManager* progressManager;
    SimpleTimer dismissTimer;
    static constexpr int AUTO_DISMISS_MS = 10000;
    bool transitionToIdleState = false;
    bool selectYes = true;
    void renderUi(Device* PDN);
};

/*
 * ColorProfilePicker — Browse and equip unlocked color palettes.
 * Entered from Idle via long press on secondary button.
 * Lists all eligible profiles + DEFAULT. Primary scrolls, secondary confirms.
 */
class ColorProfilePicker : public State {
public:
    explicit ColorProfilePicker(Player* player, ProgressManager* progressManager);
    ~ColorProfilePicker();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToIdle();

private:
    Player* player;
    ProgressManager* progressManager;
    bool transitionToIdleState = false;
    bool displayIsDirty = false;
    bool ledPreviewRequested = false;
    int cursorIndex = 0;
    std::vector<int> profileList;  // game type values (-1 = DEFAULT)
    void buildProfileList();
    void renderUi(Device* PDN);
};

/*
 * BoonAwarded — Celebration state when player beats hard mode.
 * Displays "BOON UNLOCKED!" message with game palette name.
 * Previews the unlocked color palette on all LEDs (cycling animation).
 * Triggers haptic celebration pattern.
 * After 5 seconds, transitions to ColorProfilePrompt for equipping.
 */
class BoonAwarded : public State {
public:
    explicit BoonAwarded(Player* player, ProgressManager* progressManager);
    ~BoonAwarded();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToColorPrompt();

private:
    Player* player;
    ProgressManager* progressManager;
    SimpleTimer celebrationTimer;
    static constexpr int CELEBRATION_DURATION_MS = 5000;
    bool transitionToColorPromptState = false;
    GameType unlockedGameType;
};
