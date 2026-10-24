#pragma once

#include "game/player.hpp"
#include <string>

/**
 * Decodes a DevicePlayerResponse protobuf body.
 *
 * Pure function with no platform dependencies — testable on native.
 */
namespace DevicePlayerDecoder {

/**
 * Decodes @p body into @p player's registration fields, replacing previous registration data.
 *
 * @return false when @p body is not a valid DevicePlayerResponse.
 */
bool decode(const std::string& body, Player& player);

}  // namespace DevicePlayerDecoder
