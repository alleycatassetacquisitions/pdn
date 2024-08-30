#pragma once

#include "simple-timer.hpp"
#include "../state-machine/state.hpp"

// Quickdraw States
StateId DORMANT = StateId(0);
StateId ACTIVATION_SEQUENCE = StateId(1);
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

class QuickdrawStateData{};

template <class QuickdrawStateData>
class QuickdrawState : public State {
  public:
  typedef QuickdrawStateData stateDataType;
    explicit QuickdrawState(StateId stateId) : State(stateId) {
      payload = new stateDataType();
    }
    ~QuickdrawState();
    QuickdrawStateData* payload;

};

class DormantStateData : QuickdrawStateData {
public:
  bool isHunter = false;
  unsigned long bountyDelay[2] = {300000, 900000};
  unsigned long overchargeDelay[2] = {180000, 300000};
  unsigned long debugDelay = 3000;
};

class Dormant : public QuickdrawState<DormantStateData> {
  
  public:
    Dormant();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToActivationSequence();

  private:
    SimpleTimer dormantTimer;
    
};

class ActivationSequenceStateData : QuickdrawStateData {
public:
  byte activateMotorCount = 0;
  bool activateMotor = false;
  const int activationStepDuration = 100;
};

class ActivationSequence : public QuickdrawState<ActivationSequenceStateData> {
  public:
  ActivationSequence();
  void onStateMounted(Device* PDN) override;
  void onStateLoop(Device* PDN) override;
  void onStateDismounted(Device* PDN) override;

  bool transitionToActivated();

private:
  SimpleTimer activationSequenceTimer;
};

class ActivatedStateData : QuickdrawStateData {
  public:
  bool isHunter = false;
};

class Activated : public QuickdrawState<ActivatedStateData> {
  
  public:
    Activated();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    void broadcast();
    void ledAnimation(Device* PDN);

    bool transitionToHandshake();

  private:
    const float smoothingPoints = 255;
    byte ledBrightness = 65;
    float pwm_val = 0;
    bool breatheUp = true;
    long idleLEDBreak = 5000;
    CRGBPalette16 currentPalette;
};

class HandshakeStateData : QuickdrawStateData {};

class Handshake : public QuickdrawState<HandshakeStateData> {
  
  public:
    Handshake();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

  private:
    HandshakeState handshakeState = HandshakeState::HANDSHAKE_TIMEOUT_START_STATE;
    bool handshakeTimedOut = false;
    const int HANDSHAKE_TIMEOUT = 5000;
    
};

class DuelAlertStateData : QuickdrawStateData {};

class DuelAlert : public QuickdrawState<DuelAlertStateData> {
  
  public:
    DuelAlert();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

  private:
    int alertFlashTime = 250;
    byte alertCount = 0;
    
};

class DuelCountdownStateData : QuickdrawStateData {};

class DuelCountdown : public QuickdrawState<DuelCountdownStateData> {
  
  public:
    DuelCountdown();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

  private:
    bool doBattle = false;
    const byte COUNTDOWN_STAGES = 4;
    byte countdownStage = COUNTDOWN_STAGES;
    int FOUR = 2000;
    int THREE = 2000;
    int TWO = 1000;
    int ONE = 3000;
    
};

class DuelStateData : QuickdrawStateData {};

class Duel : public QuickdrawState<DuelStateData> {
  
  public:
    Duel();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

  private:
    bool captured = false;
    bool wonBattle = false;
    bool startDuelTimer = true;
    bool sendZapSignal = true;
    bool duelTimedOut = false;
    bool bvbDuel = false;
    const int DUEL_TIMEOUT = 4000;
    
};

class WinStateData : QuickdrawStateData {};

class Win : public QuickdrawState<WinStateData> {
  
  public:
    Win();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

  private:
    bool startBattleFinish = true;
    byte finishBattleBlinkCount = 0;
    byte FINISH_BLINKS = 10;
    
};

class LoseStateData : QuickdrawStateData {};

class Lose : public QuickdrawState<LoseStateData> {
  
  public:
    Lose();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    
    private:
      bool initiatePowerDown = true;

};