#pragma once

#ifdef UNIT_TEST

#include "game/minigame.hpp"
#include "state/state.hpp"

/*
 * TestMiniGame — a trivial 2-state minigame for testing the SM Manager.
 *
 * State 0: TestMiniGameIntro — waits until triggerComplete() is called.
 * State 1: TestMiniGameComplete — sets outcome and signals readyForResume.
 *
 * This class is guarded by UNIT_TEST and should never appear in production builds.
 */

class TestMiniGame;  // Forward declaration

static const int TEST_MINIGAME_INTRO_STATE_ID = 200;
static const int TEST_MINIGAME_COMPLETE_STATE_ID = 201;

class TestMiniGameIntro : public State {
public:
    TestMiniGameIntro() : State(TEST_MINIGAME_INTRO_STATE_ID) {}
    void onStateMounted(Device* PDN) override {}
    void onStateLoop(Device* PDN) override {}
    void onStateDismounted(Device* PDN) override {}
};

class TestMiniGameComplete : public State {
public:
    TestMiniGameComplete(TestMiniGame* game, MiniGameResult resultToSet) :
        State(TEST_MINIGAME_COMPLETE_STATE_ID),
        game_(game),
        resultToSet(resultToSet)
    {
    }

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override {}
    void onStateDismounted(Device* PDN) override {}
    bool isTerminalState() override { return true; }

private:
    TestMiniGame* game_;
    MiniGameResult resultToSet;
};

class TestMiniGame : public MiniGame {
public:
    TestMiniGame(Device* device, MiniGameResult resultToSet = MiniGameResult::WON) :
        MiniGame(device, GameType::SIGNAL_ECHO, "TEST MINIGAME"),
        resultToSet_(resultToSet)
    {
    }

    void populateStateMap() override {
        auto* intro = new TestMiniGameIntro();
        auto* complete = new TestMiniGameComplete(this, resultToSet_);

        // Transition from intro to complete is triggered externally via triggerComplete()
        intro->addTransition(new StateTransition(
            [this]() { return triggerFlag; },
            complete
        ));

        stateMap.push_back(intro);
        stateMap.push_back(complete);
    }

    /*
     * Trigger the transition from Intro to Complete state.
     * On the next loop(), the state machine will transition
     * and the Complete state will set the outcome + readyForResume.
     */
    void triggerComplete() {
        triggerFlag = true;
    }

private:
    MiniGameResult resultToSet_;
    bool triggerFlag = false;
};

// Implementation of TestMiniGameComplete::onStateMounted
// (defined after TestMiniGame is fully declared to access its methods)
inline void TestMiniGameComplete::onStateMounted(Device* PDN) {
    MiniGameOutcome out;
    out.result = resultToSet;
    out.score = 42;
    game_->setOutcome(out);
    game_->setReadyForResume(true);
}

#endif // UNIT_TEST
