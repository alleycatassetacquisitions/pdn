#include "game/player-provider.hpp"

Player* PlayerProvider::getPlayer() { return &player; }

const Player* PlayerProvider::getPlayer() const { return &player; }

bool PlayerProvider::hasPlayer() const { return player.hasRegistrationProfile(); }

void PlayerProvider::clear() { player.clearRegistrationProfile(); }
