#pragma once

#include "state/state-machine.hpp"
#include "apps/symbol-match/symbol-match-states.hpp"
#include "apps/symbol-match/symbol-manager.hpp"
#include "utils/debounced-condition.hpp"

class SymbolWirelessManager;

class SymbolMatchApp : public StateMachine {
public:
    /** Symbol-match app: Selection → SymbolIdle → MatchSuccess → Selection; a refresh skips
     * MatchSuccess. */
    SymbolMatchApp(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                   SymbolWirelessManager* symbolWirelessManager);
    /** Deletes the owned SymbolManager. */
    ~SymbolMatchApp();

    void populateStateMap() override;

    /** Starts each visit with a fresh game-over window. */
    void onStateMounted(Device* device) override {
        gameOverDebounce.reset();
        StateMachine::onStateMounted(device);
    }

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    DebouncedCondition gameOverDebounce;

    static constexpr unsigned long GAME_OVER_DEBOUNCE_MS = 500;
};
