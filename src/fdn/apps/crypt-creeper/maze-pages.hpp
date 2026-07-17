#pragma once

#include <cstdint>

namespace CryptCreeperMaze {

static constexpr int kGridCols      = 8;
static constexpr int kGridRows      = 4;
static constexpr int kCellSize      = 16;
static constexpr int kGlyphBaseline = 17;
static constexpr int kNumPages      = 9;
static constexpr uint8_t kKeepPos   = 255;

// Page indices (0-based): 0=page1 … 8=page9
static constexpr int kPage1 = 0;
static constexpr int kPage2 = 1;
static constexpr int kPage3 = 2;
static constexpr int kPage4 = 3;
static constexpr int kPage5 = 4;
static constexpr int kPage6 = 5;
static constexpr int kPage7 = 6;
static constexpr int kPage8 = 7;
static constexpr int kPage9 = 8;

// Coordinates are 0-based (user spec row/col 1 = index 0).
struct PageLink {
    int8_t targetPage;  // -1 = blocked
    uint8_t exitRow;    // kKeepPos = don't check row
    uint8_t exitCol;    // kKeepPos = don't check col
    uint8_t entryRow;   // kKeepPos = use edge default
    uint8_t entryCol;   // kKeepPos = use edge default
};

inline bool portalMatches(const PageLink& link, int row, int col) {
    if (link.targetPage < 0) {
        return false;
    }
    if (link.exitRow != kKeepPos && static_cast<uint8_t>(row) != link.exitRow) {
        return false;
    }
    if (link.exitCol != kKeepPos && static_cast<uint8_t>(col) != link.exitCol) {
        return false;
    }
    return true;
}

struct MazePage {
    uint8_t walls[kGridRows];
    PageLink linkLeft;
    PageLink linkRight;
    PageLink linkUp;
    PageLink linkDown;
    int8_t startRow;
    int8_t startCol;
    int8_t goalRow;
    int8_t goalCol;
};

inline bool isWall(const MazePage& page, int row, int col) {
    if (row < 0 || row >= kGridRows || col < 0 || col >= kGridCols) {
        return true;
    }
    return (page.walls[row] & (1U << col)) != 0;
}

inline bool isWalkable(const MazePage& page, int row, int col) {
    return !isWall(page, row, col);
}

// Sequence: 1 -> 2 -> 3 -> 4 -> 3 -> 5 -> 6 -> 7 -> 8 -> 9
// Portal exits/entries use 0-based grid indices matching user row/col numbering.
static constexpr MazePage kPages[kNumPages] = {
    // Page 1 — exit right (row 3 / corridor row 2)
    {
        {0xFF, 0xFF, 0x01, 0xFF},
        {-1, 0, 0, 0, 0},
        {kPage2, kKeepPos, kKeepPos, kKeepPos, 0},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        2, 1, -1, -1,
    },
    // Page 2 — exit up at col 6 (index 5)
    {
        {0xDF, 0xDF, 0xC0, 0xFF},
        {kPage1, kKeepPos, kKeepPos, kKeepPos, 7},
        {-1, 0, 0, 0, 0},
        {kPage3, kKeepPos, 5, 3, 5},
        {-1, 0, 0, 0, 0},
        -1, -1, -1, -1,
    },
    // Page 3 — up col 6 -> 4; left row 2 -> 5 (after return from 4)
    {
        {0xD7, 0xD0, 0xDF, 0xDF},
        {kPage5, 1, 0, 1, 7},  // enter page 5 at row 2 col 8 (0-based 1,7)
        {-1, 0, 0, 0, 0},
        {kPage4, 0, 5, 3, 5},
        {kPage2, kKeepPos, 5, 3, 5},
        -1, -1, -1, -1,
    },
    // Page 4 — down col 4 (index 3) -> 3
    {
        {0xFF, 0xC7, 0xD7, 0xD7},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        {kPage3, 3, 3, 0, 3},
        -1, -1, -1, -1,
    },
    // Page 5 — left row 4 (index 3) -> 6; right row 2 col 8 -> 3
    {
        {0xFF, 0x1F, 0xC3, 0xF8},
        {kPage6, 3, 0, 3, 7},
        {kPage3, 1, 7, 1, 0},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        -1, -1, -1, -1,
    },
    // Page 6 — left row 3 (index 2) -> 7; right row 4 col 8 -> 5
    {
        {0xC7, 0xD7, 0xD0, 0x1F},
        {kPage7, 2, 0, 2, 7},
        {kPage5, 3, 7, 3, 0},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        -1, -1, -1, -1,
    },
    // Page 7 — down col 7 (index 6) -> 8; right row 3 col 8 -> 6
    {
        {0xFF, 0xFF, 0x3F, 0xBF},
        {kPage6, 2, 7, 2, 0},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        {kPage8, 3, 6, 0, 6},
        -1, -1, -1, -1,
    },
    // Page 8 — left row 2 (index 1) -> 9; up row 1 col 7 -> 7
    {
        {0xBF, 0xB8, 0x83, 0xFF},
        {kPage9, 1, 0, 1, 7},
        {kPage7, kKeepPos, kKeepPos, kKeepPos, 7},
        {kPage7, 0, 6, 3, 6},
        {-1, 0, 0, 0, 0},
        -1, -1, -1, -1,
    },
    // Page 9 — goal
    {
        {0xFF, 0x00, 0xFF, 0xFF},
        {kPage8, kKeepPos, kKeepPos, 1, 7},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        {-1, 0, 0, 0, 0},
        -1, -1, 1, 1,
    },
};

}  // namespace CryptCreeperMaze
