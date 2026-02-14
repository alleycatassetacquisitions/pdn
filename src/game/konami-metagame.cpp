#include "../../include/game/konami-metagame.hpp"
#include "device/drivers/logger.hpp"

// PlaceholderState implementation
PlaceholderState::PlaceholderState(int stateId) :
    State(stateId)
{
}

PlaceholderState::~PlaceholderState() {
}

void PlaceholderState::onStateMounted(Device* PDN) {
    LOG_I("PlaceholderState", "Mounted state %d", getStateId());
    transitionToIdleState = true;
}

void PlaceholderState::onStateLoop(Device* PDN) {
}

void PlaceholderState::onStateDismounted(Device* PDN) {
    LOG_I("PlaceholderState", "Dismounted state %d", getStateId());
    transitionToIdleState = false;
}

bool PlaceholderState::transitionToIdle() {
    return transitionToIdleState;
}

// KonamiMetaGame implementation
KonamiMetaGame::KonamiMetaGame(Player* player) :
    StateMachine(KONAMI_METAGAME_APP_ID),
    player(player)
{
}

KonamiMetaGame::~KonamiMetaGame() {
    player = nullptr;
}

void KonamiMetaGame::populateStateMap() {
    // Create 35 placeholder states for now
    // [0] = Handshake
    PlaceholderState* handshake = new PlaceholderState(KONAMI_HANDSHAKE);

    // [1-7] = EasyLaunch (7 games)
    PlaceholderState* easyLaunch[7];
    for (int i = 0; i < 7; i++) {
        easyLaunch[i] = new PlaceholderState(KONAMI_EASY_LAUNCH_START + i);
    }

    // [8-14] = ReplayEasy (7 games)
    PlaceholderState* replayEasy[7];
    for (int i = 0; i < 7; i++) {
        replayEasy[i] = new PlaceholderState(KONAMI_REPLAY_EASY_START + i);
    }

    // [15-21] = HardLaunch (7 games)
    PlaceholderState* hardLaunch[7];
    for (int i = 0; i < 7; i++) {
        hardLaunch[i] = new PlaceholderState(KONAMI_HARD_LAUNCH_START + i);
    }

    // [22-28] = MasteryReplay (7 games)
    PlaceholderState* masteryReplay[7];
    for (int i = 0; i < 7; i++) {
        masteryReplay[i] = new PlaceholderState(KONAMI_MASTERY_REPLAY_START + i);
    }

    // [29] = ButtonAwarded
    PlaceholderState* buttonAwarded = new PlaceholderState(KONAMI_BUTTON_AWARDED);

    // [30] = BoonAwarded
    PlaceholderState* boonAwarded = new PlaceholderState(KONAMI_BOON_AWARDED);

    // [31] = GameOverReturn
    PlaceholderState* gameOverReturn = new PlaceholderState(KONAMI_GAME_OVER_RETURN);

    // [32] = CodeEntry
    PlaceholderState* codeEntry = new PlaceholderState(KONAMI_CODE_ENTRY);

    // [33] = CodeAccepted
    PlaceholderState* codeAccepted = new PlaceholderState(KONAMI_CODE_ACCEPTED);

    // [34] = CodeRejected
    PlaceholderState* codeRejected = new PlaceholderState(KONAMI_CODE_REJECTED);

    // Add all states to the state map in order
    stateMap.push_back(handshake);

    for (int i = 0; i < 7; i++) {
        stateMap.push_back(easyLaunch[i]);
    }

    for (int i = 0; i < 7; i++) {
        stateMap.push_back(replayEasy[i]);
    }

    for (int i = 0; i < 7; i++) {
        stateMap.push_back(hardLaunch[i]);
    }

    for (int i = 0; i < 7; i++) {
        stateMap.push_back(masteryReplay[i]);
    }

    stateMap.push_back(buttonAwarded);
    stateMap.push_back(boonAwarded);
    stateMap.push_back(gameOverReturn);
    stateMap.push_back(codeEntry);
    stateMap.push_back(codeAccepted);
    stateMap.push_back(codeRejected);
}
