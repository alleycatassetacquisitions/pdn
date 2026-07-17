#pragma once

#include <cstdint>
#include "apps/crypt-creeper/maze-pages.hpp"

namespace CryptCreeperTutorial {

using CryptCreeperMaze::PageLink;
using CryptCreeperMaze::kKeepPos;
using CryptCreeperMaze::kGridCols;
using CryptCreeperMaze::kGridRows;
using CryptCreeperMaze::kCellSize;
using CryptCreeperMaze::kGlyphBaseline;

static constexpr int kNumPages = 4;

struct TutorialPage {
    uint8_t walls[kGridRows];
    PageLink linkLeft;
    PageLink linkRight;
    PageLink linkUp;
    int8_t startRow;
    int8_t startCol;
    int8_t goalRow;
    int8_t goalCol;
};

inline bool isWall(const TutorialPage& page, int row, int col) {
    if (row < 0 || row >= kGridRows || col < 0 || col >= kGridCols) {
        return true;
    }
    return (page.walls[row] & (1U << col)) != 0;
}

inline bool isWalkable(const TutorialPage& page, int row, int col) {
    return !isWall(page, row, col);
}

// Page indices 0-3
static constexpr int kPage1 = 0;
static constexpr int kPage2 = 1;
static constexpr int kPage3 = 2;
static constexpr int kPage4 = 3;

static constexpr TutorialPage kPages[kNumPages] = {
    // Page 1 — corridor along row 3, exit right
    {
        {0xFF, 0xFF, 0x01, 0xFF},
        {-1, 0, 0, 0, 0},
        {kPage2, 2, kKeepPos, 2, 0},
        {-1, 0, 0, 0, 0},
        2, 1, -1, -1,
    },
    // Page 2 — L turn; exit up col 6 to page 3
    {
        {0xDF, 0xDF, 0xC0, 0xFF},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        {kPage3, 0, 5, 3, 5},
        2, 0, -1, -1,
    },
    // Page 3 — exit left row 2 col 1 to page 4
    {
        {0xFF, 0xC0, 0xDF, 0xDF},
        {kPage4, 1, 0, 1, 7},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        -1, -1, -1, -1,
    },
    // Page 4 — goal star at row 2 col 2
    {
        {0xFF, 0x01, 0xFF, 0xFF},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        -1, -1, 1, 1,
    },
};

}  // namespace CryptCreeperTutorial
