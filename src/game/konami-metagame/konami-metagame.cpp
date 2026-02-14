#include "game/konami-metagame/konami-metagame.hpp"
#include "game/konami-metagame/konami-metagame-states.hpp"

KonamiMetaGame::KonamiMetaGame(Player* player) :
    StateMachine(KONAMI_METAGAME_APP_ID),
    player(player)
{
}

void KonamiMetaGame::populateStateMap() {
    // TODO: Stub - other agents implementing the actual states
    // States will include:
    // - FdnModeSelect (choose easy/hard/mastery)
    // - FdnGameLaunch (launch minigame with correct difficulty)
    // - FdnReward (award button/boon after win)
    // - KonamiCodeEntry (after all 7 buttons collected)

    // For now, create a minimal stub state to prevent segfaults
    // The stateMap must have at least one state for StateMachine::initialize() to work
    // This will be replaced by actual states when other agents implement them
    KonamiStubState* stub = new KonamiStubState();
    stateMap.push_back(stub);
}
