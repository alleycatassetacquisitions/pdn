#pragma once

inline int calculateBoost(int confirmedSupporters) {
    static const int boostTable[] = {0, 30, 50, 65, 78, 88, 96, 102, 107, 111, 115};
    static const int maxIndex = 10;
    int index = (confirmedSupporters > maxIndex) ? maxIndex : confirmedSupporters;
    if (index < 0) index = 0;
    return boostTable[index];
}
