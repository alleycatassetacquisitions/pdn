#pragma once

#include <gtest/gtest.h>

#include <optional>

#include "utility-tests.hpp"
#include "wireless/tls-clock-gate.hpp"
#include "wireless/wireless-types.hpp"

// Catches a client that sends without TLS: only an address whose scheme is
// https:// lets a request out.
inline void isHttpsAddressAcceptsOnlyTheHttpsScheme() {
    EXPECT_TRUE(isHttpsAddress("https://api.example/v1"));
    EXPECT_FALSE(isHttpsAddress("http://localhost:3000")) << "plain HTTP was sent to";
    EXPECT_FALSE(isHttpsAddress("api.example/v1")) << "an address without a scheme was sent to";
    EXPECT_FALSE(isHttpsAddress("http://api.example/?next=https://x"))
    << "https:// after the scheme was taken for it";
    EXPECT_FALSE(isHttpsAddress("")) << "an empty address was sent to";
}

// Catches a client that treats a redirect as an answer: redirects are not
// followed, so a 3xx fails like any other non-2xx and keeps its status.
inline void httpStatusFailureRefusesRedirects() {
    EXPECT_FALSE(httpStatusFailure(200).has_value());
    EXPECT_FALSE(httpStatusFailure(299).has_value());
    for (int status : {300, 301, 302, 307, 308, 399}) {
        const std::optional<WirelessErrorInfo> failure = httpStatusFailure(status);
        ASSERT_TRUE(failure.has_value()) << "HTTP " << status << " was a success";
        EXPECT_EQ(failure->code, WirelessError::INVALID_RESPONSE);
        EXPECT_EQ(failure->httpStatus, status);
    }
    ASSERT_TRUE(httpStatusFailure(199).has_value());
    EXPECT_EQ(httpStatusFailure(499)->code, WirelessError::INVALID_RESPONSE);
    EXPECT_EQ(httpStatusFailure(500)->code, WirelessError::SERVER_ERROR);
}

class TlsClockGateTests : public testing::Test {
public:
    /** Starts with the clock of a cold-booted FDN: uptime 0, wall clock at 1970. */
    void SetUp() override {
        SimpleTimer::setPlatformClock(&clock);
        clock.setTime(0);
        clock.setUnixTimeMs(0);
    }

    /** Leaves no fake clock behind for later suites. */
    void TearDown() override { SimpleTimer::setPlatformClock(nullptr); }

    static constexpr int64_t SET_CLOCK_MS = 1792359000000LL;
    FakePlatformClock clock;
    TlsClockGate gate;
};

// Catches a TLS request sent before SNTP sets the clock, and a hold with no
// end: the request is held for the whole wait and then given up, so an upload
// fails and keeps its records instead of waiting forever.
TEST_F(TlsClockGateTests, holdsWithoutAClockThenGivesUp) {
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::HOLD);
    clock.advance(TlsClockGate::CLOCK_WAIT_MS);
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::HOLD) << "given up before the wait ended";
    clock.setUnixTimeMs(MIN_VALID_UTC_UNIX_MS - 1);
    clock.advance(1);
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::GIVE_UP) << "a 2024 clock counted as set";

    EXPECT_EQ(gate.check(), TlsClockGate::Decision::HOLD) << "the next request got no wait";
}

// Catches a gate that stays shut once the clock is set, or that charges one
// request's wait to the next: a set clock lets every request through at once.
TEST_F(TlsClockGateTests, sendsOnceTheClockIsSet) {
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::HOLD);
    clock.advance(TlsClockGate::CLOCK_WAIT_MS - 1);
    clock.setUnixTimeMs(SET_CLOCK_MS);
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::SEND);

    clock.setUnixTimeMs(0);
    clock.advance(2);
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::HOLD) << "the wait that ended in a send ran on";
}

// Catches a wait carried over from a Wi-Fi trip that ended: after reset the
// next held request waits in full.
TEST_F(TlsClockGateTests, resetStartsAFreshWait) {
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::HOLD);
    clock.advance(TlsClockGate::CLOCK_WAIT_MS);
    gate.reset();
    clock.advance(1);
    EXPECT_EQ(gate.check(), TlsClockGate::Decision::HOLD);
}
