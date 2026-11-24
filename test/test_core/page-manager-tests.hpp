#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>
#include <string>
#include <vector>

#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "game/player.hpp"
#include "game/quickdraw-states.hpp"
#include "wireless/page-cmd.hpp"
#include "wireless/page-display.hpp"
#include "wireless/page-manager.hpp"
#include "wireless/page-relay.hpp"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SaveArg;

inline const uint8_t* pageJson(const std::string& json, std::vector<uint8_t>& storage) {
    storage.assign(json.begin(), json.end());
    return storage.data();
}

TEST(PageCmdParseTest, parsesFields) {
    std::vector<uint8_t> buf;
    const char* raw =
        R"({"id":"abc","text":"Hello","duration":7,"scroll":true,"player_id":"p1","role":"hunter","neocorp":"helix"})";
    PageCmd cmd;
    ASSERT_TRUE(parsePageCmdJson(pageJson(raw, buf), buf.size(), cmd));
    EXPECT_EQ(cmd.id, "abc");
    EXPECT_EQ(cmd.text, "Hello");
    EXPECT_EQ(cmd.durationSeconds, 7);
    EXPECT_TRUE(cmd.scroll);
    EXPECT_EQ(cmd.playerId, "p1");
    EXPECT_EQ(cmd.role, "hunter");
    EXPECT_EQ(cmd.neocorp, "helix");
}

TEST(PageCmdParseTest, clampsDuration) {
    std::vector<uint8_t> buf;
    PageCmd cmd;
    ASSERT_TRUE(parsePageCmdJson(pageJson(R"({"text":"x","duration":999999})", buf), buf.size(), cmd));
    EXPECT_EQ(cmd.durationSeconds, PAGE_MAX_DURATION_SECONDS);
    ASSERT_TRUE(parsePageCmdJson(pageJson(R"({"text":"x","duration":-5})", buf), buf.size(), cmd));
    EXPECT_EQ(cmd.durationSeconds, 0);
}

TEST(PageCmdFilterTest, emptyStampsAccept) {
    Player player("1234", Allegiance::HELIX, true);
    PageCmd cmd;
    cmd.text = "hi";
    EXPECT_TRUE(pageCmdMatchesPlayer(cmd, &player));
}

TEST(PageCmdFilterTest, roleAndNeocorpMustMatch) {
    Player hunter("1234", Allegiance::HELIX, true);
    Player bounty("5678", Allegiance::ENDLINE, false);

    PageCmd hunterHelix;
    hunterHelix.role = "hunter";
    hunterHelix.neocorp = "helix";
    EXPECT_TRUE(pageCmdMatchesPlayer(hunterHelix, &hunter));
    EXPECT_FALSE(pageCmdMatchesPlayer(hunterHelix, &bounty));

    PageCmd bountyEndline;
    bountyEndline.role = "bounty";
    bountyEndline.neocorp = "endline";
    EXPECT_TRUE(pageCmdMatchesPlayer(bountyEndline, &bounty));
    EXPECT_FALSE(pageCmdMatchesPlayer(bountyEndline, &hunter));
}

TEST(PageCmdFilterTest, playerIdMatchesUserId) {
    Player player("p9", Allegiance::ALLEYCAT, false);
    PageCmd cmd;
    cmd.playerId = "p9";
    EXPECT_TRUE(pageCmdMatchesPlayer(cmd, &player));
    cmd.playerId = "other";
    EXPECT_FALSE(pageCmdMatchesPlayer(cmd, &player));
}

// ---------------------------------------------------------------------------
// PageManager (PDN inbox)
// ---------------------------------------------------------------------------

class PageManagerTestSuite : public ::testing::Test {
protected:
    void SetUp() override {
        player = new Player("p1", Allegiance::HELIX, true);
        pageManager.setLocalPlayer(player);
    }

    void TearDown() override {
        delete player;
        player = nullptr;
    }

    Player* player = nullptr;
    PageManager pageManager;
};

TEST_F(PageManagerTestSuite, processPacketQueuesPendingAndDedupes) {
    std::vector<uint8_t> buf;
    const char* raw = R"({"id":"page-1","text":"Ping","duration":3})";
    ASSERT_EQ(pageManager.processPacket(nullptr, pageJson(raw, buf), buf.size()), 0);
    PageCmd page;
    ASSERT_TRUE(pageManager.takePendingPage(page));
    EXPECT_EQ(page.text, "Ping");
    EXPECT_FALSE(pageManager.hasPendingPage());

    EXPECT_EQ(pageManager.processPacket(nullptr, pageJson(raw, buf), buf.size()), 0);
    EXPECT_FALSE(pageManager.hasPendingPage());
}

TEST_F(PageManagerTestSuite, filteredPageIsNotRemembered) {
    std::vector<uint8_t> buf;
    const char* raw =
        R"({"id":"page-2","text":"Bounty only","duration":2,"role":"bounty"})";
    EXPECT_EQ(pageManager.processPacket(nullptr, pageJson(raw, buf), buf.size()), 0);
    EXPECT_FALSE(pageManager.hasPendingPage());
    EXPECT_FALSE(pageManager.hasSeen("page-2"));
}

TEST_F(PageManagerTestSuite, oversizedPacketRejected) {
    std::vector<uint8_t> big(PageManager::MAX_OPAQUE_PAYLOAD + 1, ' ');
    EXPECT_EQ(pageManager.processPacket(nullptr, big.data(), big.size()), -1);
    EXPECT_FALSE(pageManager.hasPendingPage());
}

TEST_F(PageManagerTestSuite, queuedPagesComeOutOldestFirst) {
    PageCmd a;
    a.id = "a";
    PageCmd b;
    b.id = "b";
    pageManager.queuePending(a);
    pageManager.queuePending(b);
    EXPECT_EQ(pageManager.pendingCount(), 2u);

    PageCmd out;
    ASSERT_TRUE(pageManager.takePendingPage(out));
    EXPECT_EQ(out.id, "a");
    ASSERT_TRUE(pageManager.takePendingPage(out));
    EXPECT_EQ(out.id, "b");
    EXPECT_FALSE(pageManager.takePendingPage(out));
}

TEST_F(PageManagerTestSuite, fullQueueDropsOldest) {
    for (size_t i = 0; i <= PageManager::MAX_QUEUED; ++i) {
        PageCmd cmd;
        cmd.id = "p" + std::to_string(i);
        pageManager.queuePending(cmd);
    }
    EXPECT_EQ(pageManager.pendingCount(), PageManager::MAX_QUEUED);
    PageCmd out;
    ASSERT_TRUE(pageManager.takePendingPage(out));
    EXPECT_EQ(out.id, "p1");
}

TEST_F(PageManagerTestSuite, pageHeldPastMaxHoldIsDropped) {
    FakePlatformClock clock;
    clock.setTime(1000);
    SimpleTimer::setPlatformClock(&clock);

    PageCmd cmd;
    cmd.id = "old";
    cmd.durationSeconds = 10;
    pageManager.queuePending(cmd);

    // Survives a match far longer than its own 10 s duration...
    clock.advance(5UL * 60UL * 1000UL);
    EXPECT_TRUE(pageManager.hasPendingPage());
    // ...but not past the hold limit.
    clock.advance(PageManager::MAX_HOLD_MS);
    EXPECT_FALSE(pageManager.hasPendingPage());

    SimpleTimer::setPlatformClock(nullptr);
}

// ---------------------------------------------------------------------------
// PageRelay (FDN) over WirelessManager
// ---------------------------------------------------------------------------

class PageRelayTestSuite : public ::testing::Test {
protected:
    void SetUp() override {
        device = new MockDevice();
        ON_CALL(*device->mockPeerComms, getPeerCommsState())
            .WillByDefault(Return(PeerCommsState::CONNECTED));
        ON_CALL(*device->mockPeerComms, getGlobalBroadcastAddress())
            .WillByDefault(Return(broadcastMac));
        ON_CALL(*device->mockPeerComms, getWifiChannel())
            .WillByDefault(Return(static_cast<uint8_t>(ESPNOW_CHANNEL)));
        ON_CALL(*device->mockPeerComms, setWifiChannel(_)).WillByDefault(Return(true));
        ON_CALL(*device->mockPeerComms, isSendQueueEmpty()).WillByDefault(Return(true));
        ON_CALL(*device->mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(0));
        ON_CALL(*device->mockHttpClient, getHttpClientState())
            .WillByDefault(Return(HttpClientState::CONNECTED));
        ON_CALL(*device->mockHttpClient, isConnected()).WillByDefault(Return(true));

        // FDN concurrent mode, as fdn/main.cpp brings it up.
        device->getWirelessManager()->beginConcurrentWifi();
        device->getWirelessManager()->attachConcurrentEspNow();

        relay.initialize(device->getWirelessManager(), &inbox);
    }

    void TearDown() override {
        delete device;
        device = nullptr;
    }

    bool ingest(const std::string& json) {
        return relay.ingest(pageJson(json, buf), buf.size());
    }

    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    MockDevice* device = nullptr;
    PageManager inbox;
    PageRelay relay;
    std::vector<uint8_t> buf;
};

TEST_F(PageRelayTestSuite, onEspNowChannelSendsWithoutHop) {
    EXPECT_CALL(*device->mockPeerComms, sendData(_, PktType::kPageCommand, _, _)).Times(1);
    EXPECT_CALL(*device->mockPeerComms, setWifiChannel(_)).Times(0);

    ASSERT_TRUE(ingest(R"({"id":"page-3","text":"All hands","duration":5})"));
    EXPECT_TRUE(inbox.hasPendingPage());
    EXPECT_FALSE(inbox.hasSeen("page-3"));

    relay.flush();
    EXPECT_TRUE(inbox.hasSeen("page-3"));
}

TEST_F(PageRelayTestSuite, offEspNowChannelPinsChannelForTheHop) {
    ON_CALL(*device->mockPeerComms, getWifiChannel()).WillByDefault(Return(static_cast<uint8_t>(11)));
    EXPECT_CALL(*device->mockPeerComms, setWifiChannel(ESPNOW_CHANNEL)).Times(1);
    EXPECT_CALL(*device->mockHttpClient, setHttpClientState(HttpClientState::DISCONNECTED))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(*device->mockHttpClient, setHttpClientState(HttpClientState::CONNECTED))
        .Times(AnyNumber());
    EXPECT_CALL(*device->mockPeerComms, sendData(_, PktType::kPageCommand, _, _)).Times(1);

    ASSERT_TRUE(ingest(R"({"id":"page-4","text":"Hop","duration":5})"));
    relay.flush();

    EXPECT_EQ(device->getWirelessManager()->getCurrentMode(), WirelessMode::WIFI_AND_ESPNOW);
}

TEST_F(PageRelayTestSuite, burstSharesOneHop) {
    ON_CALL(*device->mockPeerComms, getWifiChannel()).WillByDefault(Return(static_cast<uint8_t>(11)));
    EXPECT_CALL(*device->mockPeerComms, setWifiChannel(ESPNOW_CHANNEL)).Times(1);
    EXPECT_CALL(*device->mockPeerComms, sendData(_, PktType::kPageCommand, _, _)).Times(2);

    ASSERT_TRUE(ingest(R"({"id":"a","text":"one"})"));
    ASSERT_TRUE(ingest(R"({"id":"b","text":"two"})"));
    relay.flush();
}

TEST_F(PageRelayTestSuite, duplicateIdRejected) {
    ASSERT_TRUE(ingest(R"({"id":"dup","text":"x"})"));
    EXPECT_FALSE(ingest(R"({"id":"dup","text":"x"})"));
    relay.flush();
    EXPECT_FALSE(ingest(R"({"id":"dup","text":"x"})"));
}

TEST_F(PageRelayTestSuite, failedRelayCanBeRetried) {
    EXPECT_CALL(*device->mockPeerComms, sendData(_, PktType::kPageCommand, _, _))
        .WillOnce(Return(-1))
        .WillOnce(Return(0));

    ASSERT_TRUE(ingest(R"({"id":"retry","text":"x"})"));
    relay.flush();
    EXPECT_FALSE(inbox.hasSeen("retry"));

    ASSERT_TRUE(ingest(R"({"id":"retry","text":"x"})"));
    relay.flush();
    EXPECT_TRUE(inbox.hasSeen("retry"));
}

TEST_F(PageRelayTestSuite, oversizedPayloadRejectedBeforeParse) {
    std::string big(PageManager::MAX_OPAQUE_PAYLOAD + 1, ' ');
    EXPECT_FALSE(ingest(big));
    EXPECT_FALSE(inbox.hasPendingPage());
}

// ---------------------------------------------------------------------------
// Page text wrapping and PageDisplayState
// ---------------------------------------------------------------------------

inline int tenPxPerChar(const char* s) { return static_cast<int>(std::strlen(s)) * 10; }

TEST(WrapPageTextTest, wrapsOnWords) {
    const auto lines = wrapPageText("hello world foo", 50, tenPxPerChar);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "hello");
    EXPECT_EQ(lines[1], "world");
    EXPECT_EQ(lines[2], "foo");
}

TEST(WrapPageTextTest, keepsShortWordsTogether) {
    const auto lines = wrapPageText("a b c d", 50, tenPxPerChar);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "a b c");
    EXPECT_EQ(lines[1], "d");
}

TEST(WrapPageTextTest, splitsWordWiderThanLine) {
    const auto lines = wrapPageText("abcdefghijkl", 50, tenPxPerChar);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "abcde");
    EXPECT_EQ(lines[1], "fghij");
    EXPECT_EQ(lines[2], "kl");
}

TEST(WrapPageTextTest, honorsNewlines) {
    const auto lines = wrapPageText("one\ntwo", 128, tenPxPerChar);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "one");
    EXPECT_EQ(lines[1], "two");
}

class PageDisplayStateTestSuite : public ::testing::Test {
protected:
    void SetUp() override {
        device.setPageManager(&inbox);
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, getWidth()).WillByDefault(Return(128));
        ON_CALL(*device.mockDisplay, getTextWidth(_)).WillByDefault(Invoke(tenPxPerChar));
        ON_CALL(*device.mockPrimaryButton,
                setButtonPress(::testing::Matcher<parameterizedCallbackFunction>(_), _, _))
            .WillByDefault(DoAll(SaveArg<0>(&clickCallback), SaveArg<1>(&clickCtx)));
    }

    void queue(const std::string& id, const std::string& text) {
        PageCmd cmd;
        cmd.id = id;
        cmd.text = text;
        cmd.durationSeconds = 10;
        inbox.queuePending(cmd);
    }

    void click() { clickCallback(clickCtx); }

    MockDevice device;
    PageManager inbox;
    PageDisplayState state{99};
    parameterizedCallbackFunction clickCallback = nullptr;
    void* clickCtx = nullptr;
};

TEST_F(PageDisplayStateTestSuite, finishesImmediatelyWithNothingPending) {
    state.onStateMounted(&device);
    EXPECT_TRUE(state.isFinished());
}

TEST_F(PageDisplayStateTestSuite, buttonStepsThroughScreensOfALongPage) {
    // 12 chars per 128 px line wraps this to 4 lines: "one two", "three four",
    // "five six", "seven eight". 3 lines per screen makes two screens.
    queue("long", "one two three four five six seven eight");
    state.onStateMounted(&device);
    ASSERT_FALSE(state.isFinished());
    ASSERT_NE(clickCallback, nullptr);

    click();
    state.onStateLoop(&device);
    EXPECT_FALSE(state.isFinished());

    click();
    state.onStateLoop(&device);
    EXPECT_TRUE(state.isFinished());
}

TEST_F(PageDisplayStateTestSuite, eachMountShowsTheNextQueuedPage) {
    queue("first", "Hi");
    queue("second", "There");

    EXPECT_CALL(*device.mockDisplay, drawText(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(*device.mockDisplay, drawText(::testing::StrEq("+1"), _, _)).Times(1);
    state.onStateMounted(&device);
    click();
    state.onStateLoop(&device);
    ASSERT_TRUE(state.isFinished());
    state.onStateDismounted(&device);

    EXPECT_TRUE(inbox.hasPendingPage());
    state.onStateMounted(&device);
    EXPECT_FALSE(state.isFinished());
    EXPECT_FALSE(inbox.hasPendingPage());
}

// ---------------------------------------------------------------------------
// Pages during the Quickdraw post-match cooldown (Sleep)
// ---------------------------------------------------------------------------

class SleepPageTestSuite : public ::testing::Test {
protected:
    static constexpr unsigned long COOLDOWN_MS = 60000UL;  // Sleep::SLEEP_DURATION

    void SetUp() override {
        clock.setTime(1000);
        SimpleTimer::setPlatformClock(&clock);
        device.setPageManager(&inbox);
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override { SimpleTimer::setPlatformClock(nullptr); }

    void queuePage() {
        PageCmd cmd;
        cmd.id = "cooldown-page";
        cmd.text = "Hello";
        inbox.queuePending(cmd);
    }

    FakePlatformClock clock;
    MockDevice device;
    PageManager inbox;
    Player player{"p1", Allegiance::HELIX, true};
    Sleep sleep{&player};
};

TEST_F(SleepPageTestSuite, cooldownKeepsCountingAcrossAPage) {
    sleep.onStateMounted(&device);
    clock.advance(20000);
    queuePage();
    ASSERT_TRUE(sleep.transitionToPageDisplay());
    sleep.onStateDismounted(&device);

    clock.advance(10000);  // reading the page
    sleep.onStateMounted(&device);
    EXPECT_FALSE(sleep.cooldownExpired());

    clock.advance(30001);  // 60001 ms since the cooldown began, not since the page
    EXPECT_TRUE(sleep.cooldownExpired());
}

TEST_F(SleepPageTestSuite, cooldownThatEndsDuringAPageIsReportedAndRestartsNextTime) {
    sleep.onStateMounted(&device);
    queuePage();
    ASSERT_TRUE(sleep.transitionToPageDisplay());
    sleep.onStateDismounted(&device);

    clock.advance(COOLDOWN_MS + 1);
    EXPECT_TRUE(sleep.cooldownExpired());  // the page state goes straight to Idle

    // The next match's cooldown starts from zero.
    sleep.onStateMounted(&device);
    EXPECT_FALSE(sleep.cooldownExpired());
}

TEST_F(SleepPageTestSuite, noPageOnceCooldownIsOver) {
    sleep.onStateMounted(&device);
    clock.advance(COOLDOWN_MS + 1);
    queuePage();
    EXPECT_FALSE(sleep.transitionToPageDisplay());
}
