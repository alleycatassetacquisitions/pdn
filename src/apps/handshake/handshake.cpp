#include "apps/handshake/handshake.hpp"
#include "state/state.hpp"

// Define a unique app ID for HandshakeApp
constexpr int HANDSHAKE_APP_ID = 2;

HandshakeApp::HandshakeApp(Player* player, MatchManager* matchManager,
                           QuickdrawWirelessManager* quickdrawWirelessManager) :
    StateMachine(HANDSHAKE_APP_ID),
    player(player),
    matchManager(matchManager),
    quickdrawWirelessManager(quickdrawWirelessManager)
{
}

HandshakeApp::~HandshakeApp() {
    player = nullptr;
    matchManager = nullptr;
    quickdrawWirelessManager = nullptr;
}

void HandshakeApp::populateStateMap() {
    // Create handshake states
    HandshakeInitiateState* handshakeInitiate = new HandshakeInitiateState(player);
    BountySendConnectionConfirmedState* bountySendCC = new BountySendConnectionConfirmedState(player, matchManager, quickdrawWirelessManager);
    HunterSendIdState* hunterSendId = new HunterSendIdState(player, matchManager, quickdrawWirelessManager);

    // Add states to state map
    stateMap.push_back(handshakeInitiate);
    stateMap.push_back(bountySendCC);
    stateMap.push_back(hunterSendId);

    // Configure transitions
    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToBountySendCC, handshakeInitiate),
            bountySendCC));

    handshakeInitiate->addTransition(
        new StateTransition(
            std::bind(&HandshakeInitiateState::transitionToHunterSendId, handshakeInitiate),
            hunterSendId));
}

bool HandshakeApp::readyForCountdown() const {
    return handshakeComplete;
}

bool HandshakeApp::timedOut() const {
    return handshakeTimedOut;
}
