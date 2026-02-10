#pragma once

#ifdef UNIT_TEST

#include "state/state-machine-manager.hpp"
#include "state/state.hpp"

/*
 * TestSwappableGame — a trivial 2-state game for testing the SM Manager.
 *
 * State 0: TestGameIntro — waits until triggerComplete() is called.
 * State 1: TestGameComplete — sets outcome and signals readyForResume.
 *
 * This class is guarded by UNIT_TEST and should never appear in production builds.
 */

class TestSwappableGame;  // Forward declaration

static const int TEST_GAME_INTRO_STATE_ID = 200;
static const int TEST_GAME_COMPLETE_STATE_ID = 201;
static const int TEST_GAME_ID = 99;

class TestGameIntro : public State {
public:
    TestGameIntro() : State(TEST_GAME_INTRO_STATE_ID) {}
    void onStateMounted(Device* PDN) override {}
    void onStateLoop(Device* PDN) override {}
    void onStateDismounted(Device* PDN) override {}
};

class TestGameComplete : public State {
public:
    TestGameComplete(TestSwappableGame* game, int resultToSet) :
        State(TEST_GAME_COMPLETE_STATE_ID),
        game_(game),
        resultToSet_(resultToSet)
    {
    }

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override {}
    void onStateDismounted(Device* PDN) override {}
    bool isTerminalState() override { return true; }

private:
    TestSwappableGame* game_;
    int resultToSet_;
};

class TestSwappableGame : public SwappableGame {
public:
    TestSwappableGame(Device* device, int resultToSet = SwappableOutcome::WON) :
        SwappableGame(device),
        resultToSet_(resultToSet)
    {
    }

    void populateStateMap() override {
        auto* intro = new TestGameIntro();
        auto* complete = new TestGameComplete(this, resultToSet_);

        intro->addTransition(new StateTransition(
            [this]() { return triggerFlag_; },
            complete
        ));

        stateMap.push_back(intro);
        stateMap.push_back(complete);
    }

    bool isReadyForResume() const override { return readyForResume_; }
    SwappableOutcome getOutcome() const override { return outcome_; }
    int getGameId() const override { return TEST_GAME_ID; }

    void setOutcome(const SwappableOutcome& o) { outcome_ = o; }
    void setReadyForResume(bool ready) { readyForResume_ = ready; }

    void triggerComplete() {
        triggerFlag_ = true;
    }

private:
    SwappableOutcome outcome_;
    bool readyForResume_ = false;
    int resultToSet_;
    bool triggerFlag_ = false;
};

inline void TestGameComplete::onStateMounted(Device* PDN) {
    SwappableOutcome out;
    out.result = resultToSet_;
    out.score = 42;
    game_->setOutcome(out);
    game_->setReadyForResume(true);
}

#endif // UNIT_TEST
