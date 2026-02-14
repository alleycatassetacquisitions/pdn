#pragma once

#include "state/state.hpp"
#include "game/player.hpp"
#include "game/fdn-game-type.hpp"
#include "utils/simple-timer.hpp"
#include <string>

/*
 * KonamiHandshake State
 *
 * This state acts as the routing brain for the Konami metagame system.
 * After an FDN handshake completes, this state reads the FDN game type
 * and routes the player to the appropriate state based on their progress:
 *
 * Decision Tree:
 * 1. If KONAMI_CODE FDN:
 *    - If allButtonsCollected → CodeEntry
 *    - Else → CodeRejected
 *
 * 2. If regular game FDN:
 *    - If hasBoon → MasteryReplay (index 22 + gameType)
 *    - If hardModeUnlocked && !hasBoon → HardLaunch (index 15 + gameType)
 *    - If hasButton → ReplayEasy (index 8 + gameType)
 *    - Else → EasyLaunch (index 1 + gameType)
 *
 * Transitions are by state map index for direct routing.
 */
class KonamiHandshake : public State {
public:
    explicit KonamiHandshake(Player* player);
    ~KonamiHandshake() override;

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    // Transition methods - these return the target state index
    bool shouldTransition() const;
    int getTargetStateIndex() const;

private:
    Player* player;
    FdnGameType fdnGameType;
    bool gameTypeReceived;
    int targetStateIndex;
    bool transitionReady;

    void handleSerialMessage(const std::string& message);
    int calculateTargetState(FdnGameType gameType);
};
