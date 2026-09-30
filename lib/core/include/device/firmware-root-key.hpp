#pragma once

#include <cstdint>

/// Pinned root public key (uncompressed P-256, X||Y), checked against every
/// signer certificate's rootSignature. All-zero until the offline signing
/// ceremony issues the real key: the zero point does not satisfy the P-256
/// curve equation. mbedtls_ecp_point_read_binary accepts the encoding
/// anyway, but no signature any real key produced can verify against a point
/// off the curve, so every check against this placeholder fails rather than
/// silently trusting an unprovisioned key.
constexpr uint8_t FIRMWARE_ROOT_PUBLIC_KEY[64] = {};
