#pragma once

/*
 * Quickdraw States - Main aggregator header
 *
 * This header includes all Quickdraw state definitions organized by category.
 * Each category is in its own header file in quickdraw-states/ directory.
 *
 * Existing code can continue to include this single header for backward compatibility.
 * For just the state ID enum, include game/quickdraw-state-ids.hpp instead.
 */

#include "game/quickdraw-state-ids.hpp"

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
