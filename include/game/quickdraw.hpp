#pragma once

#include "device.hpp"
#include "player.hpp"
#include "match.hpp"
#include "state-machine.hpp"
#include "quickdraw-states.hpp"
#include "quickdraw-resources.hpp"
#include "http-client-interface.hpp"
#include "storage-interface.hpp"
#define MATCH_SIZE sizeof(Match)

class Quickdraw : public StateMachine {
public:
    Quickdraw(Player *player, Device *PDN);
    ~Quickdraw();

    void populateStateMap() override;
    static Image getImageForAllegiance(Allegiance allegiance, ImageType whichImage);
    
    HttpClientInterface* getHttpClient() { return httpClient; }

private:
    std::vector<Match> matches;
    int numMatches = 0;
    MatchManager* matchManager;
    Player *player;
    HttpClientInterface* httpClient;
    StorageInterface* storageManager;
};
