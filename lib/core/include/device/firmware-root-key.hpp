#pragma once

#include <cstdint>

/// Pinned root public key (uncompressed P-256, X||Y), checked against every
/// signer certificate's rootSignature. All-zero until the offline signing
/// ceremony issues the real key: the zero point is not a valid P-256 point.
/// mbedtls_ecp_point_read_binary accepts it (it only parses the encoding),
/// but mbedtls_ecdsa_verify rejects it with MBEDTLS_ERR_ECP_INVALID_KEY, so
/// verification still fails closed rather than silently trusting an
/// unprovisioned key.
constexpr uint8_t FIRMWARE_ROOT_PUBLIC_KEY[64] = {};
