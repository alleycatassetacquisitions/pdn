#pragma once

#include "../device/device.hpp"
#include "../player.hpp"
#include "../match.hpp"
#include "../include/state-machine/state-machine.hpp"
#include "quickdraw-states.hpp"
#include "../simple-timer.hpp"

#define MAX_MATCHES 1000 // Maximum number of matches allowed

#define MATCH_SIZE sizeof(Match)

// Global includes
CRGBPalette16 bountyColors = CRGBPalette16(
    CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Orange, CRGB::Red, CRGB::Red,
    CRGB::Red, CRGB::Orange, CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red,
    CRGB::Orange, CRGB::Red, CRGB::Red, CRGB::Red);

CRGBPalette16 hunterColors = CRGBPalette16(
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkBlue,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen,
    CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkGreen, CRGB::DarkGreen);

CRGBPalette16 idleColors = CRGBPalette16(
  CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue, 
  CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow, 
  CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue, 
  CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow
);

class Quickdraw : public StateMachine<QuickdrawState<QuickdrawStateData>>
{
        SimpleTimer stateTimer;
        Device* PDN;

        Match matches[MAX_MATCHES];
        int numMatches = 0;

        String currentMatchId;
        String currentOpponentId;

    public:
        Quickdraw();
        ~Quickdraw() override;
        std::vector<QuickdrawState<QuickdrawStateData>> populateStateMap() override;
        void onStateMounting(QuickdrawState<QuickdrawStateData>* state) override;
        void onStateDismounting(QuickdrawState<QuickdrawStateData>* state) override;
        void onStateLooping(QuickdrawState<QuickdrawStateData>* state) override;

        Player playerInfo;

        void addMatch(String currentMatchId, String currentOpponentId);

        void quickDrawGame();
        void setupActivation();
        bool shouldActivate();
        bool activationSequence();
        void activationIdle();
        void activationOvercharge();
        bool initiateHandshake();
        bool handshake();
        void alertDuel();
        void duelCountdown();
        void duel();
        void duelOver();

        void updateScore(boolean win);

        int wins = 0;

        void setPlayerInfo(Player player);
        void setPlayerInfo(String playerJson);
};