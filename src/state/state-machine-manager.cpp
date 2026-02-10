#include "state/state-machine-manager.hpp"

StateMachineManager::StateMachineManager(Device* device) :
    device(device)
{
}

StateMachineManager::~StateMachineManager() {
    if (swappedSM) {
        delete swappedSM;
        swappedSM = nullptr;
    }
}

void StateMachineManager::setDefaultStateMachine(StateMachine* sm) {
    defaultSM = sm;
}

void StateMachineManager::pauseAndLoad(SwappableGame* game, int resumeStateIndex) {
    if (!defaultSM || !game) return;

    // Dismount the current state on the default SM
    if (defaultSM->getCurrentState()) {
        defaultSM->getCurrentState()->onStateDismounted(device);
    }

    // Store resume point
    resumeStateIndex_ = resumeStateIndex;

    // Take ownership of the swappable game
    swappedSM = game;
    swappedSM->initialize();
    swapped = true;
}

void StateMachineManager::resumePrevious() {
    if (!swapped || !swappedSM || !defaultSM) return;

    // Store outcome from the completed game
    lastOutcome_ = swappedSM->getOutcome();
    lastGameId_ = swappedSM->getGameId();

    // Dismount the swapped game's current state
    if (swappedSM->getCurrentState()) {
        swappedSM->getCurrentState()->onStateDismounted(device);
    }

    // Delete the game (we own it)
    delete swappedSM;
    swappedSM = nullptr;

    // Resume the default SM at the stored index
    defaultSM->skipToState(resumeStateIndex_);
    swapped = false;
}

void StateMachineManager::loop() {
    if (swapped && swappedSM) {
        swappedSM->loop();
        if (swappedSM->isReadyForResume()) {
            resumePrevious();
        }
    } else if (defaultSM) {
        defaultSM->loop();
    }
}

StateMachine* StateMachineManager::getActive() const {
    if (swapped && swappedSM) {
        return swappedSM;
    }
    return defaultSM;
}

bool StateMachineManager::isSwapped() const {
    return swapped;
}

SwappableOutcome StateMachineManager::getLastOutcome() const {
    return lastOutcome_;
}

int StateMachineManager::getLastGameId() const {
    return lastGameId_;
}
