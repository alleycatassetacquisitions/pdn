#pragma once
#include <gtest/gtest.h>
#include "game/chain-boost.hpp"

class ChainBoostTests : public testing::Test {};

inline void zeroSupportersGiveZeroBoost(ChainBoostTests* t) {
    EXPECT_EQ(calculateBoost(0), 0);
}

inline void oneSupporterGives30msBoost(ChainBoostTests* t) {
    EXPECT_EQ(calculateBoost(1), 30);
}

inline void threeSupportersGive65msBoost(ChainBoostTests* t) {
    EXPECT_EQ(calculateBoost(3), 65);
}

inline void tenSupportersGive115msBoost(ChainBoostTests* t) {
    EXPECT_EQ(calculateBoost(10), 115);
}

inline void moreThanTenSupportersCapsAt115ms(ChainBoostTests* t) {
    EXPECT_EQ(calculateBoost(15), 115);
}
