#pragma once

#include <gtest/gtest.h>

#include "device/firmware-verify.hpp"
#include "firmware-test-keys.hpp"

#include <cstring>

TEST(FirmwareVerifyTest, acceptsAWellFormedOffer) {
    FirmwareOfferPayload offer = makeSignedOffer(/*generation=*/1);
    EXPECT_EQ(verifyOffer(offer, TEST_ROOT_PUBLIC_KEY, 1), FirmwareResult::OK);
}

TEST(FirmwareVerifyTest, rejectsACertTheRootDidNotSign) {
    FirmwareOfferPayload offer = makeSignedOffer(1);
    offer.cert.publicKey[0] ^= 0xFF;  // cert no longer matches its signature
    EXPECT_EQ(verifyOffer(offer, TEST_ROOT_PUBLIC_KEY, 1), FirmwareResult::BAD_CERT);
}

TEST(FirmwareVerifyTest, rejectsACertBelowTheGenerationFloor) {
    FirmwareOfferPayload offer = makeSignedOffer(/*generation=*/1);
    EXPECT_EQ(verifyOffer(offer, TEST_ROOT_PUBLIC_KEY, 2), FirmwareResult::STALE_CERT);
}

TEST(FirmwareVerifyTest, rejectsASignatureOverADifferentHash) {
    // The substitution case: the image signature is valid, but for another
    // image. Checking hash and signature separately would pass this.
    FirmwareOfferPayload offer = makeSignedOffer(1);
    FirmwareOfferPayload other = makeSignedOfferForHash(1, OTHER_IMAGE_SHA);
    memcpy(offer.imageSignature, other.imageSignature, FIRMWARE_SIG_LENGTH);
    EXPECT_EQ(verifyOffer(offer, TEST_ROOT_PUBLIC_KEY, 1), FirmwareResult::BAD_SIGNATURE);
}
