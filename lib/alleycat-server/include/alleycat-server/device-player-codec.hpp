#pragma once

#include "game/player.hpp"
#include "game/record-key.hpp"
#include <string>

/**
 * Decodes a DevicePlayerResponse protobuf body.
 *
 * Pure function with no platform dependencies — testable on native.
 */
namespace DevicePlayerCodec {

/**
 * Decodes @p body into @p player's registration fields, replacing previous registration data,
 * and its record key into @p recordKey. A key of a length other than RECORD_KEY_SIZE
 * leaves @p recordKey not held and does not fail the decode.
 *
 * @return false when @p body is not a valid DevicePlayerResponse; @p recordKey is then
 * cleared.
 */
bool decode(const std::string& body, Player& player, RecordKey& recordKey);

}  // namespace DevicePlayerCodec
