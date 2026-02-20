#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "utility-tests.hpp"   // FakePlatformClock
#include "state/instructions-state.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AnyNumber;

// ============================================
// Helpers
// ============================================

static InstructionsPage makeInstructionsPage(std::initializer_list<std::string> lines) {
    InstructionsPage page;
    int i = 0;
    for(const auto& l : lines) page.lines[i++] = l;
    page.lineCount = static_cast<uint8_t>(lines.size());
    return page;
}

// ============================================
// Fixture
// ============================================

static constexpr unsigned long INSTRUCTIONS_PAGE_TIMEOUT = 3000;
static constexpr unsigned long INSTRUCTIONS_PAST_TIMEOUT = INSTRUCTIONS_PAGE_TIMEOUT + 1;

class InstructionsStateTestSuite : public testing::Test {
public:

    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);

        // Allow all display calls to chain by default (lenient)
        EXPECT_CALL(*device.mockDisplay, invalidateScreen())
            .Times(AnyNumber()).WillRepeatedly(Return(device.mockDisplay));
        EXPECT_CALL(*device.mockDisplay, setGlyphMode(_))
            .Times(AnyNumber()).WillRepeatedly(Return(device.mockDisplay));
        EXPECT_CALL(*device.mockDisplay, drawText(_, _))
            .Times(AnyNumber()).WillRepeatedly(Return(device.mockDisplay));
        EXPECT_CALL(*device.mockDisplay, drawText(_, _, _, _))
            .Times(AnyNumber()).WillRepeatedly(Return(device.mockDisplay));
        EXPECT_CALL(*device.mockDisplay, drawButton(_, _, _))
            .Times(AnyNumber()).WillRepeatedly(Return(device.mockDisplay));
        EXPECT_CALL(*device.mockDisplay, render())
            .Times(AnyNumber());

        // Button callbacks are a no-op by default
        EXPECT_CALL(*device.mockPrimaryButton, removeButtonCallbacks())
            .Times(AnyNumber());
        EXPECT_CALL(*device.mockSecondaryButton, removeButtonCallbacks())
            .Times(AnyNumber());
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Mount the state and capture both button callbacks.
    void mountAndCaptureButtons(InstructionsState& state) {
        EXPECT_CALL(*device.mockPrimaryButton, setButtonPress(_, _, _))
            .WillOnce(Invoke([this](parameterizedCallbackFunction cb, void* ctx, ButtonInteraction) {
                primaryCb  = cb;
                primaryCtx = ctx;
            }));
        EXPECT_CALL(*device.mockSecondaryButton, setButtonPress(_, _, _))
            .WillOnce(Invoke([this](parameterizedCallbackFunction cb, void* ctx, ButtonInteraction) {
                secondaryCb  = cb;
                secondaryCtx = ctx;
            }));
        state.onStateMounted(&device);
    }

    void pressPrimary()   { if(primaryCb)   primaryCb(primaryCtx); }
    void pressSecondary() { if(secondaryCb) secondaryCb(secondaryCtx); }

    void advancePastTimeout() { fakeClock->advance(INSTRUCTIONS_PAST_TIMEOUT); }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice device;

    parameterizedCallbackFunction primaryCb  = nullptr;
    void*                         primaryCtx = nullptr;
    parameterizedCallbackFunction secondaryCb  = nullptr;
    void*                         secondaryCtx = nullptr;
};

// ============================================
// No-pages: immediate menu
// ============================================

inline void noPagesMountsDirectlyToMenu(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Yes", "No" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // Secondary should fire immediately — menu is active from the start
    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());
}

inline void noPageTimerNotStartedWhenNoPages(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Yes", "No" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // Even after the timeout would have fired, the loop should not disturb menu state
    s->advancePastTimeout();
    state.onStateLoop(&s->device);

    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());
}

// ============================================
// Button-driven paging
// ============================================

inline void primaryButtonAdvancesPage(InstructionsStateTestSuite* s) {
    InstructionsPage pages[2] = {
        makeInstructionsPage({"Page one"}),
        makeInstructionsPage({"Page two"}),
    };
    InstructionsConfig config{ pages, 2, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // Still paging after one press (two pages total)
    s->pressPrimary();
    s->pressSecondary();
    ASSERT_FALSE(state.optionOneSelectedEvent());
    ASSERT_FALSE(state.optionTwoSelectedEvent());
}

inline void primaryButtonOnLastPageEntersMenu(InstructionsStateTestSuite* s) {
    InstructionsPage pages[1] = { makeInstructionsPage({"Only page"}) };
    InstructionsConfig config{ pages, 1, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // Pressing primary on the only page should enter the menu
    s->pressPrimary();
    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());
}

inline void primaryButtonInMenuCyclesMenuIndex(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // Default: menuIndex = 0 → secondary fires optionOne
    // After one primary press: menuIndex = 1 → secondary fires optionTwo
    s->pressPrimary();
    s->pressSecondary();
    ASSERT_FALSE(state.optionOneSelectedEvent());
    ASSERT_TRUE(state.optionTwoSelectedEvent());
}

inline void primaryButtonWrapsMenuIndexBackToZero(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // 0 → 1 → 0
    s->pressPrimary();
    s->pressPrimary();
    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());
    ASSERT_FALSE(state.optionTwoSelectedEvent());
}

inline void secondaryButtonIgnoredWhilePaging(InstructionsStateTestSuite* s) {
    InstructionsPage pages[2] = {
        makeInstructionsPage({"Page one"}),
        makeInstructionsPage({"Page two"}),
    };
    InstructionsConfig config{ pages, 2, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // Still on page 0 — secondary should be a no-op
    s->pressSecondary();
    ASSERT_FALSE(state.optionOneSelectedEvent());
    ASSERT_FALSE(state.optionTwoSelectedEvent());
}

// ============================================
// Timer-driven paging
// ============================================

inline void timerAdvancesPageAfterTimeout(InstructionsStateTestSuite* s) {
    InstructionsPage pages[2] = {
        makeInstructionsPage({"Page one"}),
        makeInstructionsPage({"Page two"}),
    };
    InstructionsConfig config{ pages, 2, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // After one timeout: advances to page 1, but still paging
    s->advancePastTimeout();
    state.onStateLoop(&s->device);
    s->pressSecondary();
    ASSERT_FALSE(state.optionOneSelectedEvent());

    // After a second timeout: all pages exhausted → enters menu
    s->advancePastTimeout();
    state.onStateLoop(&s->device);
    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());
}

inline void timerOnSinglePageEntersMenuAfterTimeout(InstructionsStateTestSuite* s) {
    InstructionsPage pages[1] = { makeInstructionsPage({"Only page"}) };
    InstructionsConfig config{ pages, 1, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    s->advancePastTimeout();
    state.onStateLoop(&s->device);

    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());
}

inline void timerDoesNotFireBeforeTimeout(InstructionsStateTestSuite* s) {
    InstructionsPage pages[1] = { makeInstructionsPage({"Only page"}) };
    InstructionsConfig config{ pages, 1, "Proceed", "Repeat" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    // Advance to just before the threshold
    s->fakeClock->advance(INSTRUCTIONS_PAGE_TIMEOUT - 1);
    state.onStateLoop(&s->device);

    // Should still be paging — secondary should be a no-op
    s->pressSecondary();
    ASSERT_FALSE(state.optionOneSelectedEvent());
}

// ============================================
// onBeforeMount callback
// ============================================

inline void onBeforeMountCallbackIsInvokedOnMount(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Yes", "No" };
    InstructionsState state(99, config);

    bool invoked = false;
    state.onBeforeMount = [&invoked]() { invoked = true; };

    s->mountAndCaptureButtons(state);
    ASSERT_TRUE(invoked);
}

inline void onBeforeMountCanInjectDynamicPageContent(InstructionsStateTestSuite* s) {
    InstructionsPage pages[1] = { makeInstructionsPage({"Static"}) };
    InstructionsConfig config{ pages, 1, "OK", "Back" };
    InstructionsState state(99, config);

    state.onBeforeMount = [&pages]() {
        pages[0].lines[0] = "Dynamic";
    };

    s->mountAndCaptureButtons(state);

    // The callback should have updated the page content before the first render
    ASSERT_EQ(pages[0].lines[0], "Dynamic");
}

inline void onBeforeMountIsOptional(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Yes", "No" };
    InstructionsState state(99, config);
    // No onBeforeMount assigned — mounting should not crash
    s->mountAndCaptureButtons(state);
    SUCCEED();
}

// ============================================
// Dismount / cleanup
// ============================================

inline void dismountResetsSelectionState(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Yes", "No" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());

    state.onStateDismounted(&s->device);

    ASSERT_FALSE(state.optionOneSelectedEvent());
    ASSERT_FALSE(state.optionTwoSelectedEvent());
}

inline void dismountRemovesButtonCallbacks(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Yes", "No" };
    InstructionsState state(99, config);
    s->mountAndCaptureButtons(state);

    EXPECT_CALL(*s->device.mockPrimaryButton,   removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*s->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    state.onStateDismounted(&s->device);
}

inline void canBeRemountedAfterDismount(InstructionsStateTestSuite* s) {
    InstructionsConfig config{ nullptr, 0, "Yes", "No" };
    InstructionsState state(99, config);

    // First mount / select / dismount cycle
    s->mountAndCaptureButtons(state);
    s->pressSecondary();
    state.onStateDismounted(&s->device);

    // Second mount — selection should be cleared
    s->mountAndCaptureButtons(state);
    ASSERT_FALSE(state.optionOneSelectedEvent());

    s->pressSecondary();
    ASSERT_TRUE(state.optionOneSelectedEvent());
}
