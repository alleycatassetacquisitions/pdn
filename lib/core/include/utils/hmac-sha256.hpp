#pragma once

#include <cstddef>
#include <cstdint>

#include "utils/sha256.hpp"

constexpr size_t HMAC_SHA256_SIZE = SHA256_DIGEST_SIZE;

/**
 * HMAC-SHA-256 (RFC 2104) of @p message under @p key, of any length, written to
 * @p tag. False, with @p tag zeroed, for a null or zero-length key or a null
 * message with a length. Wipes its key block and inner digest, and each hash
 * wipes its own state; only Sha256's round variables are left on the stack.
 */
[[nodiscard]] bool computeHmacSha256(const uint8_t* key, size_t keyLength, const uint8_t* message,
                                     size_t messageLength, uint8_t (&tag)[HMAC_SHA256_SIZE]);
