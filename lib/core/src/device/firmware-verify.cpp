#include "device/firmware-verify.hpp"

#include "device/drivers/logger.hpp"

#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

#include <cstring>

namespace {

const char* TAG = "FirmwareVerify";

/// Verifies `signature` (raw R||S, 64 bytes) over SHA-256(`data`, `len`)
/// under the uncompressed P-256 public key `publicKey` (X||Y, 64 bytes).
/// Hashing happens inside this call so the digest a caller checks against
/// is always the same one the signature is checked against — binding the
/// two instead of trusting a hash computed or compared elsewhere.
bool verifyP256(const uint8_t* publicKey, const uint8_t* data, size_t len,
                const uint8_t* signature) {
    uint8_t hash[32];
    if (mbedtls_sha256(data, len, hash, 0) != 0) {
        LOG_E(TAG, "sha256 failed");
        return false;
    }

    uint8_t uncompressedPoint[65];
    uncompressedPoint[0] = 0x04;
    std::memcpy(&uncompressedPoint[1], publicKey, 64);

    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (rc == 0) {
        rc = mbedtls_ecp_point_read_binary(&grp, &q, uncompressedPoint, sizeof(uncompressedPoint));
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&r, signature, 32);
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&s, signature + 32, 32);
    }
    if (rc == 0) {
        rc = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &q, &r, &s);
    }
    if (rc != 0) {
        LOG_E(TAG, "mbedtls verify failed, rc=%d", rc);
    }
    const bool verified = rc == 0;

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return verified;
}

}  // namespace

bool sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    return mbedtls_sha256(data, len, out, 0) == 0;
}

FirmwareResult verifyOffer(const FirmwareOfferPayload& offer, const uint8_t* rootPublicKey,
                           uint8_t minGeneration) {
    if (rootPublicKey == nullptr) {
        LOG_E(TAG, "rootPublicKey is null");
        return FirmwareResult::BAD_CERT;
    }
    // Order matters: authenticate the delegation before trusting anything it
    // carries, and check the floor before spending a second elliptic curve
    // verify.
    if (!verifyP256(rootPublicKey, reinterpret_cast<const uint8_t*>(&offer.cert),
                    offsetof(SignerCert, rootSignature), offer.cert.rootSignature)) {
        LOG_E(TAG, "root did not sign this certificate");
        return FirmwareResult::BAD_CERT;
    }
    if (offer.cert.generation < minGeneration) {
        LOG_E(TAG, "cert generation below the device floor");
        return FirmwareResult::STALE_CERT;
    }
    // The signed span is imageSha256|imageLength|chunkSize|chunkCount — the
    // field's own contract in peer-comms-types.hpp — so a relay cannot
    // rewrite the declared length or chunk framing once the image is
    // signed; binding just the hash would leave those free to tamper with.
    const uint8_t* signedStart =
        reinterpret_cast<const uint8_t*>(&offer) + offsetof(FirmwareOfferPayload, imageSha256);
    const size_t signedLength =
        offsetof(FirmwareOfferPayload, cert) - offsetof(FirmwareOfferPayload, imageSha256);
    if (!verifyP256(offer.cert.publicKey, signedStart, signedLength, offer.imageSignature)) {
        LOG_E(TAG, "signer did not sign this image");
        return FirmwareResult::BAD_SIGNATURE;
    }
    return FirmwareResult::OK;
}
