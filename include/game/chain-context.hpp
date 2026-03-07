#pragma once

enum class ChainRole { CHAMPION, SUPPORTER };

struct ChainContext {
    int position = 0;
    int chainLength = 1;
    int confirmedSupporters = 0;
    ChainRole role = ChainRole::CHAMPION;
    bool detectionComplete = false;
    bool hasConfirmed = false;
};
