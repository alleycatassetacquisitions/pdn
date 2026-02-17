#pragma once

/*
 * Quickdraw States - Main aggregator header
 *
 * This header includes all Quickdraw state definitions organized by category.
 * Each category is in its own header file in quickdraw-states/ directory.
 *
 * Existing code can continue to include this single header for backward compatibility.
 */

enum QuickdrawStateId {
    PLAYER_REGISTRATION = 0,
    FETCH_USER_DATA = 1,
    CONFIRM_OFFLINE = 2,
    CHOOSE_ROLE = 3,
    WELCOME_MESSAGE = 5,
    SLEEP = 6,
    AWAKEN_SEQUENCE = 7,
    IDLE = 8,
    HANDSHAKE_INITIATE_STATE = 9,
    BOUNTY_SEND_CC_STATE = 10,
    HUNTER_SEND_ID_STATE = 11,
    CONNECTION_SUCCESSFUL = 12,
    DUEL_COUNTDOWN = 13,
    DUEL = 14,
    DUEL_PUSHED = 15,
    DUEL_RECEIVED_RESULT = 16,
    DUEL_RESULT = 17,
    WIN = 18,
    LOSE = 19,
    UPLOAD_MATCHES = 20,
    FDN_DETECTED = 21,
    FDN_COMPLETE = 22,
    COLOR_PROFILE_PROMPT = 23,
    COLOR_PROFILE_PICKER = 24,
    FDN_REENCOUNTER = 25,
    KONAMI_PUZZLE = 26,
    CONNECTION_LOST = 27,
    BOON_AWARDED = 28,
    KONAMI_CODE_ENTRY = 29,
    KONAMI_CODE_ACCEPTED = 30,
    KONAMI_CODE_REJECTED = 31,
    GAME_OVER_RETURN_IDLE = 32,
    KONAMI_SCAN_SEQUENCE = 33,
    KONAMI_MASTERY_COMPLETE = 34,
    KONAMI_HARD_MODE_PROGRESS = 35
};

// Include all state category headers
#include "game/quickdraw-states/boot-states.hpp"
#include "game/quickdraw-states/role-states.hpp"
#include "game/quickdraw-states/idle-states.hpp"
#include "game/quickdraw-states/handshake-states.hpp"
#include "game/quickdraw-states/connection-states.hpp"
#include "game/quickdraw-states/duel-states.hpp"
#include "game/quickdraw-states/result-states.hpp"
#include "game/quickdraw-states/upload-states.hpp"
#include "game/quickdraw-states/fdn-states.hpp"
#include "game/quickdraw-states/color-states.hpp"
#include "game/quickdraw-states/konami-states.hpp"

// Include Konami code states (external to quickdraw-states directory)
#include "game/konami-states/konami-code-entry.hpp"
#include "game/konami-states/konami-code-result.hpp"
