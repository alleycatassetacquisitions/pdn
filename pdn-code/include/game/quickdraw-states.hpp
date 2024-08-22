#include <vector>
#include "../state-machine/State.hpp"

// Quickdraw States
StateId INITIATE = StateId(0);
StateId DORMANT = StateId(1);
StateId ACTIVATED = StateId(2);
StateId HANDSHAKE = StateId(3);
StateId DUEL_ALERT = StateId(4);
StateId DUEL_COUNTDOWN = StateId(5);
StateId DUEL = StateId(6);
StateId WIN = StateId(7);
StateId LOSE = StateId(8);

// STATE - HANDSHAKE
/**
Handshake states:
0 - start timer to delay sending handshake signal for 1000 ms
1 - wait for handshake delay to expire
2 - start timeout timer
3 - begin sending shake message and check for timeout
**/
enum class HandshakeState : uint8_t
{
  HANDSHAKE_TIMEOUT_START_STATE = 0,
  HANDSHAKE_SEND_ROLE_SHAKE_STATE = 1,
  HANDSHAKE_WAIT_ROLE_SHAKE_STATE = 2,
  HANDSHAKE_RECEIVED_ROLE_SHAKE_STATE = 3,
  HANDSHAKE_STATE_FINAL_ACK = 4
};

class QuickdrawState : public State {
  public:
    QuickdrawState(StateId stateId) : State(stateId) {};
};

class Initiate : public QuickdrawState {
  
  public:
    Initiate() : QuickdrawState(INITIATE){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);
    
};

class Dormant : public QuickdrawState {
  
  public:
    Dormant() : QuickdrawState(DORMANT){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);

  private:
    unsigned long bountyDelay[2] = {300000, 900000};
    unsigned long overchargeDelay[2] = {180000, 300000};
    unsigned long debugDelay = 3000;
    bool activationInitiated = false;
    bool beginActivationSequence = true;

    byte activateMotorCount = 0;
    bool activateMotor = false;
    
};

class Activated : public QuickdrawState {
  
  public:
    Activated() : QuickdrawState(ACTIVATED){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);

  private:
    const float smoothingPoints = 255;
    byte ledBrightness = 65;
    float pwm_val = 0;
    bool breatheUp = true;
    long idleLEDBreak = 5000;
    byte msgDelay = 0;
    
};

class Handshake : public QuickdrawState {
  
  public:
    Handshake() : QuickdrawState(HANDSHAKE){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);

  private:
    HandshakeState handshakeState = HandshakeState::HANDSHAKE_TIMEOUT_START_STATE;
    bool handshakeTimedOut = false;
    const int HANDSHAKE_TIMEOUT = 5000;
    
};

class DuelAlert : public QuickdrawState {
  
  public:
    DuelAlert() : QuickdrawState(DUEL_ALERT){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);

  private:
    int alertFlashTime = 250;
    byte alertCount = 0;
    
};

class DuelCountdown : public QuickdrawState {
  
  public:
    DuelCountdown() : QuickdrawState(DUEL_COUNTDOWN){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);

  private:
    bool doBattle = false;
    const byte COUNTDOWN_STAGES = 4;
    byte countdownStage = COUNTDOWN_STAGES;
    int FOUR = 2000;
    int THREE = 2000;
    int TWO = 1000;
    int ONE = 3000;
    
};

class Duel : public QuickdrawState {
  
  public:
    Duel() : QuickdrawState(DUEL){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);

  private:
    bool captured = false;
    bool wonBattle = false;
    bool startDuelTimer = true;
    bool sendZapSignal = true;
    bool duelTimedOut = false;
    bool bvbDuel = false;
    const int DUEL_TIMEOUT = 4000;
    
};

class Win : public QuickdrawState {
  
  public:
    Win() : QuickdrawState(WIN){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);

  private:
    bool startBattleFinish = true;
    byte finishBattleBlinkCount = 0;
    byte FINISH_BLINKS = 10;
    
};

class Lose : public QuickdrawState {
  
  public:
    Lose() : QuickdrawState(LOSE){
      
    };

    void onStateMounted(Device PDN);
    void onStateLoop(Device PDN);
    void onStateDismounted(Device PDN);
    
    private:
      bool initiatePowerDown = true;

};