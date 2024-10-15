#pragma once

#include "device.hpp"
#include "player.hpp"
#include "match.hpp"
#include "state-machine.hpp"
#include "quickdraw-states.hpp"
#include "quickdraw-resources.hpp"

#define MATCH_SIZE sizeof(Match)

// Global includes


class Quickdraw : public StateMachine {
    std::vector<Match> matches;
    int numMatches = 0;

    Player *player;

public:
    Quickdraw(Player *player, Device *PDN);

    ~Quickdraw();

    void populateStateMap() override;
};
