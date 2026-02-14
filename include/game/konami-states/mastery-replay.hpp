#pragma once

#include "state/state.hpp"
#include "device/device-types.hpp"

/*
 * MasteryReplay state - shown when a player who has already mastered a game
 * reconnects to that FDN. Allows them to select Easy or Hard mode replay.
 *
 * State map positions: [22..28] - one instance per game type
 *
 * Behavior:
 * - OLED displays: "[GAME NAME] / -- MASTERED -- / > Easy Mode / Hard Mode / UP: cycle / DOWN: select"
 * - UP button: toggle highlight between Easy Mode and Hard Mode
 * - DOWN button: select highlighted mode and transition
 * - Easy selected -> transition to stateMap index `8 + gameType` (ButtonUnlockedReplayEasy)
 * - Hard selected -> transition to stateMap index `15 + gameType` (HardLaunch)
 * - No rewards on replay - pure fun mode
 */

class MasteryReplay : public State {
public:
    MasteryReplay(int stateId, GameType gameType);
    ~MasteryReplay() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToEasyMode();
    bool transitionToHardMode();

private:
    GameType gameType;
    bool easyModeSelected = true;  // Start with easy mode highlighted
    bool transitionToEasyModeState = false;
    bool transitionToHardModeState = false;
    bool displayIsDirty = false;

    void renderUi(Device* PDN);
};
