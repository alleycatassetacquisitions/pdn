#include "../device/device.hpp"
#include "../player.hpp"
#include "../match.hpp"
#include "../include/game/game-state.hpp"

#include <iostream>
#include <string>

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

class Game {
    public:
        Game() {
            state_map = populateStateMap();
            current_state = &state_map[0];
        }
        std::vector<GameState> populateStateMap();
    private:
        GameState* current_state;
        // initial state is 0 in the list here
        std::vector<GameState> state_map;
};

class Quickdraw : Game {

    // Only valid within the state of Quickdraw, so encapsulated here
    class RoleInfo {
        public:
            enum Role {
                HUNTER,
                BOUNTY,
                MINER
            };

            CRGBPalette16 colorPalatte;

            RoleInfo(Role role) : role(role){
                switch (role)
                {
                    case HUNTER: colorPalatte = hunterColors; break;
                    case BOUNTY: colorPalatte = bountyColors; break;
                    default: assert(false); 
                }
            }

            void printData() const {
                std::cout << getRoleName();
            }

        private:
            Role role;

            std::string getRoleName() const {
                switch (role) {
                    case HUNTER: return "HUNTER";
                    case BOUNTY: return "BOUNTY";
                    case MINER: return "MINER";
                    default: return "UNKNOWN";
                }
            }

            int getRoleNum() const {
                int val = -1;
                switch (role) {
                    case HUNTER: val = 0; break;
                    case BOUNTY: val = 1; break;
                    case MINER: val = 2; break;
                    default: assert(false);
                }
                return 1 << val;
            }
    };

    //
    public:
        void populateStateTransitions();
        

};

CRGBPalette16 idleColors = CRGBPalette16(
  CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue, 
  CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow, 
  CRGB::DarkGreen, CRGB::DarkBlue, CRGB::DarkGreen, CRGB::DarkBlue, 
  CRGB::Red, CRGB::Yellow, CRGB::Red, CRGB::Yellow
);


// STATE - DUEL
bool captured = false;
bool wonBattle = false;
bool startDuelTimer = true;
bool sendZapSignal = true;
bool duelTimedOut = false;
bool bvbDuel = false;
const int DUEL_TIMEOUT = 4000;

// STATE - WIN
bool startBattleFinish = true;
byte finishBattleBlinkCount = 0;
byte FINISH_BLINKS = 10;

// STATE - LOSE
bool initiatePowerDown = true;

bool reset = false;

// ScoreDataStructureThings
//String userID = "init_uuid";
String current_match_id = "init_match_uuid";
String current_opponent_id = "init_opponent_uuid";

#define MAX_MATCHES 1000 // Maximum number of matches allowed

#define MATCH_SIZE sizeof(Match)

Match matches[MAX_MATCHES];
int numMatches = 0;

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