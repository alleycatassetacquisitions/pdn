#pragma once

#include <cstdint>

/// Pinned root public key (uncompressed P-256, X||Y), checked against every
/// signer certificate's rootSignature. All-zero until the public half of the
/// passphrase-derived root (make_root_key.py --from-passphrase) is pasted
/// here; until then no device accepts any offer. The zero point does not
/// satisfy the P-256 curve equation, so every check against this placeholder
/// fails rather than silently trusting an unprovisioned key.
constexpr uint8_t FIRMWARE_ROOT_PUBLIC_KEY[64] = {};
