#pragma once

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>

#include "device/drivers/peer-comms-types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Test-only key material: a fixed root keypair and signer keypair, generated
// from a fixed seed so every run signs and verifies the same bytes. Never
// used outside this suite.
namespace firmware_test_keys {

struct Keypair {
    uint8_t publicKey[64];
    uint8_t privateKey[32];
};

/// Aborts with a diagnostic on an mbedTLS failure. A silent fixture failure
/// here would surface as a baffling failure in an unrelated test case.
inline void checkMbedtls(int rc, const char* what) {
    if (rc != 0) {
        std::fprintf(stderr, "firmware-test-keys: %s failed, rc=%d\n", what, rc);
        std::abort();
    }
}

/// Fills `output` from a fixed byte pattern, standing in for an entropy
/// source so key generation is reproducible run to run.
inline int fixedEntropy(void*, unsigned char* output, size_t len) {
    static const uint8_t seed[] = {0x50, 0x44, 0x4e, 0x2d, 0x66, 0x77, 0x2d, 0x74,
                                   0x65, 0x73, 0x74, 0x2d, 0x73, 0x65, 0x65, 0x64};
    for (size_t i = 0; i < len; i++) {
        output[i] = seed[i % sizeof(seed)];
    }
    return 0;
}

/// Deterministic RNG shared by every key generation and signature in this
/// fixture: one seed, so the whole run is reproducible byte for byte.
inline mbedtls_ctr_drbg_context* fixtureRng() {
    static mbedtls_ctr_drbg_context ctx;
    static const int seeded = [] {
        mbedtls_ctr_drbg_init(&ctx);
        const unsigned char personalization[] = "pdn-firmware-test-fixture";
        return mbedtls_ctr_drbg_seed(&ctx, fixedEntropy, nullptr, personalization,
                                     sizeof(personalization) - 1);
    }();
    checkMbedtls(seeded, "mbedtls_ctr_drbg_seed");
    return &ctx;
}

inline Keypair generateKeypair() {
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi d;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&d);
    checkMbedtls(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1), "mbedtls_ecp_group_load");
    checkMbedtls(mbedtls_ecp_gen_keypair(&grp, &d, &q, mbedtls_ctr_drbg_random, fixtureRng()),
                 "mbedtls_ecp_gen_keypair");

    Keypair kp{};
    checkMbedtls(mbedtls_mpi_write_binary(&d, kp.privateKey, sizeof(kp.privateKey)),
                 "mbedtls_mpi_write_binary(privateKey)");
    uint8_t uncompressedPoint[65];
    size_t olen = 0;
    checkMbedtls(mbedtls_ecp_point_write_binary(&grp, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen,
                                                uncompressedPoint, sizeof(uncompressedPoint)),
                 "mbedtls_ecp_point_write_binary");
    std::memcpy(kp.publicKey, &uncompressedPoint[1], sizeof(kp.publicKey));

    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return kp;
}

inline const Keypair& rootKeypair() {
    static const Keypair kp = generateKeypair();
    return kp;
}

inline const Keypair& signerKeypair() {
    static const Keypair kp = generateKeypair();
    return kp;
}

/// Raw P-256 R||S signature (64 bytes, no DER) over SHA-256(data, len).
inline void signRaw(const uint8_t privateKey[32], const uint8_t* data, size_t len,
                    uint8_t signatureOut[64]) {
    uint8_t hash[32];
    checkMbedtls(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), data, len, hash),
                 "mbedtls_md sha256");

    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    checkMbedtls(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1), "mbedtls_ecp_group_load");
    checkMbedtls(mbedtls_mpi_read_binary(&d, privateKey, 32), "mbedtls_mpi_read_binary(privateKey)");
    checkMbedtls(
        mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof(hash), mbedtls_ctr_drbg_random, fixtureRng()),
        "mbedtls_ecdsa_sign");
    checkMbedtls(mbedtls_mpi_write_binary(&r, signatureOut, 32), "mbedtls_mpi_write_binary(r)");
    checkMbedtls(mbedtls_mpi_write_binary(&s, signatureOut + 32, 32), "mbedtls_mpi_write_binary(s)");

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
}

/// A signer certificate for `generation`, root-signed over
/// keyId|generation|publicKey|label — the same span verifyOffer checks.
inline SignerCert signCert(uint8_t generation) {
    SignerCert cert{};
    cert.keyId[3] = 1;
    cert.generation = generation;
    std::memcpy(cert.publicKey, signerKeypair().publicKey, sizeof(cert.publicKey));
    std::strncpy(cert.label, "test-signer", sizeof(cert.label));
    signRaw(rootKeypair().privateKey, reinterpret_cast<const uint8_t*>(&cert),
            offsetof(SignerCert, rootSignature), cert.rootSignature);
    return cert;
}

/// A fully signed offer for `generation` carrying `hash` as its image
/// digest, with the delegation cert and image signature both valid.
inline FirmwareOfferPayload buildOffer(uint8_t generation, const uint8_t hash[32]) {
    FirmwareOfferPayload offer{};
    offer.command = static_cast<uint8_t>(FirmwareCmd::OFFER);
    std::memcpy(offer.imageSha256, hash, FIRMWARE_SHA256_LENGTH);
    offer.imageLength = 4096;
    offer.chunkSize = 1322;
    offer.chunkCount = 4;
    offer.cert = signCert(generation);

    const uint8_t* signedStart =
        reinterpret_cast<const uint8_t*>(&offer) + offsetof(FirmwareOfferPayload, imageSha256);
    const size_t signedLength =
        offsetof(FirmwareOfferPayload, cert) - offsetof(FirmwareOfferPayload, imageSha256);
    signRaw(signerKeypair().privateKey, signedStart, signedLength, offer.imageSignature);
    return offer;
}

}  // namespace firmware_test_keys

inline constexpr uint8_t TEST_IMAGE_SHA[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};

inline constexpr uint8_t OTHER_IMAGE_SHA[32] = {
    0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
    0x10, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

/// Not compile-time (the underlying key is generated at first use), but
/// every read after that first use sees the same fixed root public key.
inline const uint8_t* const TEST_ROOT_PUBLIC_KEY = firmware_test_keys::rootKeypair().publicKey;

/// A real P-256 point that never signed a cert as the root: the wrong-key
/// case for a test that verifyOffer rejects a good offer under any key
/// other than the one it was actually chained to.
inline const uint8_t* const TEST_SIGNER_PUBLIC_KEY = firmware_test_keys::signerKeypair().publicKey;

inline FirmwareOfferPayload makeSignedOffer(uint8_t generation) {
    return firmware_test_keys::buildOffer(generation, TEST_IMAGE_SHA);
}

inline FirmwareOfferPayload makeSignedOfferForHash(uint8_t generation, const uint8_t hash[32]) {
    return firmware_test_keys::buildOffer(generation, hash);
}
