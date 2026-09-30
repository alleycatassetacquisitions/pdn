#pragma once

#include <cstddef>
#include <cstdint>

#include "device/drivers/peer-comms-types.hpp"

/// SHA-256 of `data`; writes the 32-byte digest to `out`. Returns false only
/// on an mbedTLS internal failure, never on caller input.
bool sha256(const uint8_t* data, size_t len, uint8_t out[32]);

/// Verifies a firmware offer's delegation chain: the root's signature over
/// the signer certificate, the certificate's generation against the
/// device's stored floor, then the certificate's key over the image
/// metadata. Checks stop at the first failure, in that order.
///
/// ECDSA signatures are malleable (both S and n-S verify for the same
/// message), so imageSignature must never be used as a dedup or replay key.
FirmwareResult verifyOffer(const FirmwareOfferPayload& offer, const uint8_t* rootPublicKey,
                           uint8_t minGeneration);
