#include "../device/device.hpp"
#include "../player.hpp"
#include "../match.hpp"
#include "../include/state-machine/state-machine.hpp"
#include "quickdraw-states.hpp"
#include "../simple-timer.hpp"

#include <iostream>
#include <string>

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

class Quickdraw : public StateMachine<QuickdrawState> {

    private:
        
        SimpleTimer stateTimer;
        Device* PDN;

        Match matches[MAX_MATCHES];
        int numMatches = 0;

    public:
        Quickdraw();
        std::vector<QuickdrawState> populateStateMap();

        Player playerInfo;

        void addMatch(bool winner_is_hunter);

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

// ScoreDataStructureThings
//String userID = "init_uuid";
String current_match_id = "init_match_uuid";
String current_opponent_id = "init_opponent_uuid";








// // Only valid within the state of Quickdraw, so encapsulated here
    // class RoleInfo {
    //     public:
    //         enum Role {
    //             HUNTER,
    //             BOUNTY,
    //         };

    //         CRGBPalette16 colorPalatte;

    //         RoleInfo(Role role) : role(role){
    //             switch (role)
    //             {
    //                 case HUNTER: colorPalatte = hunterColors; break;
    //                 case BOUNTY: colorPalatte = bountyColors; break;
    //                 default: assert(false); 
    //             }
    //         }

    //         void printData() const {
    //             std::cout << getRoleName();
    //         }

    //     private:
    //         Role role;

    //         std::string getRoleName() const {
    //             switch (role) {
    //                 case HUNTER: return "HUNTER";
    //                 case BOUNTY: return "BOUNTY";
    //                 default: return "UNKNOWN";
    //             }
    //         }

    //         int getRoleNum() const {
    //             int val = -1;
    //             switch (role) {
    //                 case HUNTER: val = 0; break;
    //                 case BOUNTY: val = 1; break;
    //                 default: assert(false);
    //             }
    //             return 1 << val;
    //         }
    // };