#pragma once

#include <cstdint>

/// Pinned root public key (uncompressed P-256, X||Y), checked against every
/// signer certificate's rootSignature. All-zero until the offline signing
/// ceremony issues the real key: the zero point is not a valid P-256 point,
/// so mbedTLS rejects it outright and verification fails closed rather than
/// silently trusting an unprovisioned key.
constexpr uint8_t FIRMWARE_ROOT_PUBLIC_KEY[64] = {};
