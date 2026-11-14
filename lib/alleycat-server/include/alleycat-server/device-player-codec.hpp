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
 * and hands the response's record key to @p recordKey. A valid profile with no record key,
 * or with one of any length other than RECORD_KEY_SIZE, still decodes: @p recordKey then
 * holds no key. @p player's registration totals take the response's server totals: each
 * one it lacks is zero, and all are when it claims more wins than matches played.
 *
 * @return false when @p body is not a valid DevicePlayerResponse; @p recordKey then holds no key.
 */
bool decode(const std::string& body, Player& player, RecordKey& recordKey);

}  // namespace DevicePlayerCodec
