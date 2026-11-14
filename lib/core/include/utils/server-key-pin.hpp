#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "utils/sha256.hpp"

/** Hex digits in a server key pin: one SHA-256 digest. */
constexpr size_t SERVER_KEY_PIN_HEX_LENGTH = SHA256_DIGEST_SIZE * 2;

/**
 * The one server key an FDN trusts: the SHA-256 of the server certificate's DER
 * SubjectPublicKeyInfo, configured as 64 hex digits. Kept free of platform code
 * so the key comparison is tested natively; the ESP32 HTTP client hands it the
 * key the server presents during the TLS handshake.
 */
class ServerKeyPin {
public:
    /** An unset pin, which matches no key. */
    ServerKeyPin() = default;

    /**
     * Parses @p hexText: exactly 64 hex digits, either case. Any other text
     * leaves the pin unset, and an unset pin matches no key.
     */
    explicit ServerKeyPin(const std::string& hexText);

    /** @return true when the configured text was 64 hex digits. */
    bool isSet() const { return pinIsSet; }

    /**
     * Hashes @p length bytes of DER SubjectPublicKeyInfo at @p spki and writes
     * the SHA-256 to @p presentedKeyHash as 64 lowercase hex digits and a
     * terminator, for the log; a null @p spki with a length writes an empty
     * string. Every byte is compared whatever the earlier ones held, so the
     * time taken says nothing about how much of a presented key matched.
     * @return true only when the pin is set and the hash equals it.
     */
    bool matchesKey(const uint8_t* spki, size_t length,
                    char (&presentedKeyHash)[SERVER_KEY_PIN_HEX_LENGTH + 1]) const;

    /**
     * The verification flags to leave on the certificate at @p depth of a
     * chain; only the leaf (depth 0) decides. A leaf whose key matched clears
     * every incoming flag, host name and dates included; any other leaf keeps
     * them and adds @p notTrustedFlag. Certificates above the leaf are cleared
     * so no CA's verdict counts; @p keyMatches is ignored there.
     * @return the flags to store back for this certificate.
     */
    static uint32_t verifiedFlags(int depth, bool keyMatches, uint32_t incomingFlags,
                                  uint32_t notTrustedFlag);

private:
    uint8_t pinnedDigest[SHA256_DIGEST_SIZE] = {};
    bool pinIsSet = false;
};
