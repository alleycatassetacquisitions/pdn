#pragma once

#include "device.hpp"
#include "../../lib/pdn-libs/player.hpp"
#include "../../lib/pdn-libs/match.hpp"
#include "state-machine.hpp"
#include <FastLED.h>
#include "quickdraw-states.hpp"

#define MATCH_SIZE sizeof(Match)

// Global includes
const CRGBPalette16 bountyColors = CRGBPalette16(
    CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Orange, CRGB::Red, CRGB::Red,
    CRGB::Red, CRGB::Orange, CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red,
    CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red);

const CRGBPalette16 hunterColors = CRGBPalette16(
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen);

const CRGBPalette16 idleColors = CRGBPalette16(
    CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow,
    CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow
);

class Quickdraw : public StateMachine {
    std::vector<Match> matches;
    int numMatches = 0;

    Player *player;

public:
    Quickdraw(Player *player, Device *PDN);

    ~Quickdraw();

    void populateStateMap() override;
};
