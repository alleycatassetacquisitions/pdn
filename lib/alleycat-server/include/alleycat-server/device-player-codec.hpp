#pragma once

#include "game/player.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

/** Size of the record key the server issues with a player's profile. */
constexpr size_t ISSUED_RECORD_KEY_SIZE = 32;

/** A record key from a lookup answer; held only if it had exactly ISSUED_RECORD_KEY_SIZE bytes. */
struct IssuedRecordKey {
    std::array<uint8_t, ISSUED_RECORD_KEY_SIZE> bytes{};
    bool held = false;
};

/**
 * Decodes a DevicePlayerResponse protobuf body.
 *
 * Pure function with no platform dependencies — testable on native.
 */
namespace DevicePlayerCodec {

/**
 * Decodes @p body into @p player's registration fields, replacing previous registration data,
 * and its record key into @p recordKey. A key of any other length leaves @p recordKey not held
 * and does not fail the decode.
 *
 * @return false when @p body is not a valid DevicePlayerResponse; @p recordKey is then not held.
 */
bool decode(const std::string& body, Player& player, IssuedRecordKey& recordKey);

}  // namespace DevicePlayerCodec
