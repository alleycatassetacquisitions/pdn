#pragma once

#include "alleycat-server/device-player.hpp"
#include <string>

/**
 * Decodes a DevicePlayerResponse protobuf body.
 *
 * Pure function with no platform dependencies — testable on native.
 */
namespace DevicePlayerDecoder {

/**
 * Decodes @p body into @p player, replacing its previous contents.
 *
 * @return false when @p body is not a valid DevicePlayerResponse.
 */
bool decode(const std::string& body, DevicePlayer& player);

}  // namespace DevicePlayerDecoder
