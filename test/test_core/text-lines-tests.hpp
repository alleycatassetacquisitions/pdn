#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "utils/text-lines.hpp"

// Ten pixels a character, so a 128-pixel limit holds twelve characters.
inline int measureTenPixelsPerCharacter(const char* text) {
    return static_cast<int>(std::strlen(text)) * 10;
}

inline std::vector<std::string> breakForTestDisplay(const char* text) {
    return breakTextIntoLines(text, 128, 3, measureTenPixelsPerCharacter);
}

inline void textLinesKeepAShortNameOnOneLine() {
    EXPECT_THAT(breakForTestDisplay("Demo"), testing::ElementsAre("Demo"));
    EXPECT_THAT(breakForTestDisplay("Twelve chars"), testing::ElementsAre("Twelve chars"));
    // A line exactly as wide as the limit fits.
    EXPECT_THAT(breakTextIntoLines("Twelve chars", 120, 3, measureTenPixelsPerCharacter),
                testing::ElementsAre("Twelve chars"));
}

inline void textLinesBreakAtASpace() {
    // Twelve characters fit, and the thirteenth is the space: it ends the
    // line and is not carried to the next one.
    EXPECT_THAT(breakForTestDisplay("Ada Lovelace King"),
                testing::ElementsAre("Ada Lovelace", "King"));
    // Twelve characters end inside "Vanderbilt", so the line backs up to the space.
    EXPECT_THAT(breakForTestDisplay("Wilhelmina Vanderbilt"),
                testing::ElementsAre("Wilhelmina", "Vanderbilt"));
    // A run of spaces at the break is dropped whole.
    EXPECT_THAT(breakForTestDisplay("Wilhelmina   Vanderbilt"),
                testing::ElementsAre("Wilhelmina", "Vanderbilt"));
}

inline void textLinesFitALongestNameWithSpacesOnThreeLines() {
    const char* name = "Maximiliana Fitzwilliam van Dyk";
    ASSERT_EQ(std::strlen(name), 31U);
    EXPECT_THAT(breakForTestDisplay(name),
                testing::ElementsAre("Maximiliana", "Fitzwilliam", "van Dyk"));
}

// Broken at spaces this name needs four lines. At twelve characters a line
// it fits three when they are filled, so it is shown whole instead of losing
// its last word. On the PDN the font is wider and uneven, so a name of many
// wide letters can still be cut.
inline void textLinesShowAllOfANameThatOnlyFitsWhenFilled() {
    const char* name = "Abigail Bernard Charles Dominic";
    ASSERT_EQ(std::strlen(name), 31U);
    EXPECT_THAT(breakForTestDisplay(name),
                testing::ElementsAre("Abigail Bern", "ard Charles", "Dominic"));
}

inline void textLinesSplitAWordWiderThanTheLimit() {
    EXPECT_THAT(breakForTestDisplay("Supercalifragilistic"),
                testing::ElementsAre("Supercalifra", "gilistic"));
    // A short word ahead of the long one keeps its own line; only the word
    // that cannot fit anywhere is split.
    EXPECT_THAT(breakForTestDisplay("Al Supercalifragilistic"),
                testing::ElementsAre("Al", "Supercalifra", "gilistic"));
}

inline void textLinesCutTextBeyondTheLastLine() {
    EXPECT_THAT(breakForTestDisplay("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP"),
                testing::ElementsAre("abcdefghijkl", "mnopqrstuvwx", "yzABCDEFGHIJ"));
    // Still too long with every line filled to the width: the end is lost.
    EXPECT_THAT(breakForTestDisplay("one two three four five six seven eight"),
                testing::ElementsAre("one two thre", "e four five", "six seven ei"));
}

inline void textLinesGiveNoLinesForNoText() {
    EXPECT_TRUE(breakForTestDisplay("").empty());
    EXPECT_TRUE(breakForTestDisplay("   ").empty());
}
