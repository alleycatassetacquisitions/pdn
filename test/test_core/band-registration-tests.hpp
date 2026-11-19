#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "apps/player-fetch/player-fetch.hpp"
#include "connect-state-callback-tests.hpp"
#include "fdn-registration-tests.hpp"
#include "game/game-session.hpp"

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

// ============================================
// PDN band registration, against a real FDN
// ============================================
//
// A PDN and an FDN, each a real coordinator over a mocked radio, the PDN's
// output jack cabled to the FDN's primary input jack: each radio delivers to
// the other and each jack carries the other's bytes. The PDN runs the
// registration app over a game session, as it boots; the FDN runs its
// registration manager with a fake band reader. A test can also forge what an
// FDN or another device would send the PDN.

class BandRegistrationTests : public testing::Test {
public:
    /** A PDN whose coordinator is the fixture's real one. */
    class PdnDevice : public MockDevice {
    public:
        /** @return the fixture's PDN coordinator. */
        RemoteDeviceCoordinator* getRemoteDeviceCoordinator() override { return rdc; }
        RemoteDeviceCoordinator* rdc = nullptr;
    };

    /** One device's radio: what it sent, what waits to be delivered, its handlers. */
    struct Radio {
        struct Frame {
            PktType type;
            std::array<uint8_t, 6> to;
            std::vector<uint8_t> data;
        };
        uint8_t* mac = nullptr;
        std::vector<Frame> sent;
        std::vector<Frame> outbox;
        std::map<PktType, std::pair<PeerCommsInterface::PacketCallback, void*>> handlers;
        std::map<PktType, std::pair<PeerCommsInterface::SendStatusCallback, void*>> statusHandlers;
    };

    /** Brings both coordinators up, builds the FDN manager and boots the PDN's registration app. */
    void SetUp() override {
        // A 10 s wait is hundreds of ticks of uninteresting radio calls; the
        // warnings for them run to megabytes and stall the test runner.
        savedGmockVerbose = GMOCK_FLAG_GET(verbose);
        GMOCK_FLAG_SET(verbose, "error");
        clock.setTime(1000);
        SimpleTimer::setPlatformClock(&clock);
        wireRadio(pdn, pdnRadio, pdnMac);
        wireRadio(fdn, fdnRadio, fdnMac);
        ON_CALL(*fdn.mockHttpClient, queueRequest(_))
        .WillByDefault(testing::Invoke([this](HttpRequest&& request) {
            requests.push_back(std::move(request));
            return true;
        }));
        MockDisplay* display = pdn.mockDisplay;
        ON_CALL(*display, invalidateScreen()).WillByDefault(testing::Invoke([this, display]() {
            screen.clear();
            return display;
        }));
        ON_CALL(*display, drawText(_, _, _))
        .WillByDefault(testing::Invoke([this, display](const char* text, int, int) {
            screen.emplace_back(text);
            return display;
        }));
        ON_CALL(*display, setGlyphMode(_)).WillByDefault(Return(display));
        ON_CALL(*display, drawButton(_, _, _)).WillByDefault(Return(display));
        ON_CALL(*display, renderGlyph(_, _, _)).WillByDefault(Return(display));
        ON_CALL(*display, drawImage(_)).WillByDefault(Return(display));
        ON_CALL(*pdn.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault(
        testing::Invoke([this](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            primaryPress = fn;
            primaryCtx = ctx;
        }));
        ON_CALL(*pdn.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault(
        testing::Invoke([this](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            secondaryPress = fn;
            secondaryCtx = ctx;
        }));

        pdn.serialManager->setOutputJack(&pdnOut);
        pdn.serialManager->setInputJack(&pdnIn);
        fdn.serialManager->setInputJack(&fdnIn);
        fdn.serialManager->setSecondaryInputJack(&fdnIn2);
        for (RemoteDeviceCoordinator* rdc : {&pdnRdc, &fdnRdc})
            rdc->setExternalConnectivityTask(true);
        pdnRdc.initialize(pdn.wirelessManager, pdn.serialManager, &pdn);
        fdnRdc.initialize(fdn.wirelessManager, fdn.serialManager, &fdn);
        pdn.rdc = &pdnRdc;

        manager = new FdnProvisioningManager(fdn.wirelessManager, &fdnRdc, &reader);
        manager->setOnViewChanged([this](const RegistrationView& changed) { fdnView = changed; });
        manager->setRegistrationScreenShown(true);
        session =
        new GameSession(playerProvider.getPlayer(), playerProvider.getRecordKey(), &pdn, nullptr);
        bandRegistration = new BandRegistration(pdn.wirelessManager, &pdnRdc, &playerProvider);
        app = new PlayerFetchApp(nullptr, &playerProvider, bandRegistration, &pdnRdc, false);
        AppConfig apps;
        apps[StateId(PLAYER_FETCH_APP_ID)] = app;
        pdn.loadAppConfig(apps, StateId(PLAYER_FETCH_APP_ID));
    }

    /** Dismounts the app, then frees what borrows the coordinators. */
    void TearDown() override {
        pdn.loadAppConfig(AppConfig(), StateId(-1));
        delete app;
        delete bandRegistration;
        delete session;
        delete manager;
        SimpleTimer::setPlatformClock(nullptr);
        GMOCK_FLAG_SET(verbose, savedGmockVerbose);
    }

    /** Records @p device's sends and handlers in @p radio. */
    static void wireRadio(MockDevice& device, Radio& radio, uint8_t* mac) {
        radio.mac = mac;
        wireRadioDefaults(device, mac);
        Radio* target = &radio;
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _))
        .WillByDefault(
        testing::Invoke([target](const uint8_t* to, PktType type, const uint8_t* data, size_t len) {
            Radio::Frame frame{type, {}, std::vector<uint8_t>(data, data + len)};
            memcpy(frame.to.data(), to, 6);
            target->sent.push_back(frame);
            target->outbox.push_back(frame);
            return 1;
        }));
        ON_CALL(*device.mockPeerComms, setPacketHandler(_, _, _))
        .WillByDefault(testing::Invoke(
        [target](PktType type, PeerCommsInterface::PacketCallback callback, void* ctx) {
            target->handlers[type] = {std::move(callback), ctx};
        }));
        ON_CALL(*device.mockPeerComms, clearPacketHandler(_))
        .WillByDefault(testing::Invoke([target](PktType type) { target->handlers.erase(type); }));
        ON_CALL(*device.mockPeerComms, setSendStatusHandler(_, _, _))
        .WillByDefault(testing::Invoke(
        [target](PktType type, PeerCommsInterface::SendStatusCallback callback, void* ctx) {
            target->statusHandlers[type] = {std::move(callback), ctx};
        }));
    }

    /** Hands @p from's queued frames for @p to's MAC, or broadcast, to @p to; all are delivered. */
    static void deliver(Radio& from, Radio& to) {
        std::vector<Radio::Frame> frames;
        frames.swap(from.outbox);
        for (const Radio::Frame& frame : frames) {
            const bool forTo = memcmp(frame.to.data(), to.mac, 6) == 0 ||
                               memcmp(frame.to.data(), MockDevice::BROADCAST_MAC, 6) == 0;
            if (forTo) receive(to, from.mac, frame.type, frame.data);
            const auto status = from.statusHandlers.find(frame.type);
            if (status != from.statusHandlers.end() && status->second.first) {
                const std::pair<PeerCommsInterface::SendStatusCallback, void*> slot =
                status->second;
                slot.first(frame.to.data(), frame.data.data(), frame.data.size(), true,
                           slot.second);
            }
        }
    }

    /** One frame of @p type from @p fromMac arriving at @p radio. */
    static void receive(Radio& radio, const uint8_t* fromMac, PktType type,
                        const std::vector<uint8_t>& data) {
        // Copied: a handler may clear its own slot while it runs.
        const auto handler = radio.handlers.find(type);
        if (handler == radio.handlers.end() || !handler->second.first) return;
        const std::pair<PeerCommsInterface::PacketCallback, void*> slot = handler->second;
        slot.first(fromMac, data.data(), data.size(), slot.second);
    }

    /** One tick of @p ms: HELLOs over the cables, the radios, every sync and the PDN's loop. */
    void step(unsigned long ms = 20) {
        clock.advance(ms);
        pdnRdc.emitHello();
        fdnRdc.emitHello();
        if (fdnCabled) {
            pumpCable(pdnOut, fdnIn);
            pumpCable(fdnIn, pdnOut);
        } else {
            pdnOut.clearOutput();
            fdnIn.clearOutput();
        }
        pdnIn.clearOutput();
        if (forgedPeer.has_value()) deliverFrame(pdnIn, hello(forgedPeer->data(), forgedType));
        deliver(pdnRadio, fdnRadio);
        deliver(fdnRadio, pdnRadio);
        pdnRdc.sync(&pdn);
        fdnRdc.sync(&fdn);
        manager->sync();
        pdn.loop();
    }

    /** Steps through @p ms. */
    void advance(unsigned long ms) {
        for (unsigned long elapsed = 0; elapsed < ms; elapsed += 20)
            step();
    }

    /** Unplugs the FDN and waits out the silent-link window. */
    void unplugFdn() {
        fdnCabled = false;
        advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 40);
    }

    /** Cables a device of @p type with radio MAC @p mac to the PDN's input jack, Connected. */
    void cableForgedPeer(const uint8_t* mac, DeviceType type) {
        forgedPeer.emplace();
        memcpy(forgedPeer->data(), mac, 6);
        forgedType = type;
        step();
        if (type == DeviceType::FDN) {
            FdnConnectionContext context{};
            context.seqId = ++forgedSeqId;
            receive(pdnRadio, mac, PktType::kFdnConnectionContext, bytesOf(context));
        } else {
            PdnConnectionContext context{};
            context.seqId = ++forgedSeqId;
            receive(pdnRadio, mac, PktType::kPdnConnectionContext, bytesOf(context));
        }
        advance(100);
    }

    /** A HELLO from @p mac as a device of @p type. */
    static std::vector<uint8_t> hello(const uint8_t* mac, DeviceType type) {
        HelloPayload payload{};
        memcpy(payload.source, mac, 6);
        payload.deviceType = static_cast<uint8_t>(type);
        return encodeFramed(payload);
    }

    /** The bytes of a packed wire struct. */
    template <class P> static std::vector<uint8_t> bytesOf(const P& packet) {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(&packet);
        return std::vector<uint8_t>(raw, raw + sizeof(P));
    }

    /** A record key frame of bytes @p fill from @p source, arriving on @p jack. */
    static void sendKey(NativeSerialDriver& jack, const uint8_t* source, uint8_t fill) {
        deliverFrame(jack,
                     applicationFrameBytes(static_cast<uint8_t>(ApplicationFrameKind::RECORD_KEY),
                                           source, std::vector<uint8_t>(RECORD_KEY_SIZE, fill)));
    }

    /** A profile for @p name, or a lookup failure, from @p fromMac. */
    void sendProfile(const uint8_t* fromMac, const char* name, bool lookupFailed = false) {
        Player player;
        player.applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID), name, Team(), {});
        PlayerProvisionPacket packet = PlayerProvisionCodec::encode(player);
        if (lookupFailed) packet.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
        packet.seqId = ++forgedSeqId;
        receive(pdnRadio, fromMac, PktType::kPlayerProvision, bytesOf(packet));
    }

    /** Taps a band at the FDN and answers its lookup with @p body. */
    void tapAndAnswer(const std::string& body) {
        reader.tapBand({0x04, 0xA2, 0x1B, 0x9C});
        advance(100);
        ASSERT_EQ(requests.size(), 1u);
        requests[0].onSuccess(body);
        advance(100);
    }

    /** @return the PDN state now mounted. */
    int pdnState() { return pdn.getActiveApp()->getCurrentState()->getStateId(); }

    /** @return true when the PDN screen drawn last holds @p text. */
    bool shows(const char* text) const {
        for (const std::string& line : screen) {
            if (line == text) return true;
        }
        return false;
    }

    /** @return the `registered` byte of the last context the PDN sent the FDN; -1 if none. */
    int lastRegisteredSentToFdn() const {
        int registered = -1;
        for (const Radio::Frame& frame : pdnRadio.sent) {
            if (frame.type != PktType::kPdnConnectionContext || memcmp(frame.to.data(), fdnMac, 6))
                continue;
            PdnConnectionContext context{};
            memcpy(&context, frame.data.data(), sizeof(context));
            registered = context.player.registered;
        }
        return registered;
    }

    /** @return true while the PDN holds a record key of bytes @p fill. */
    bool holdsKeyOf(uint8_t fill) {
        const RecordKey* key = playerProvider.getRecordKey();
        if (!key->isHeld()) return false;
        for (size_t i = 0; i < RECORD_KEY_SIZE; ++i) {
            if (key->getBytes()[i] != fill) return false;
        }
        return true;
    }

    // What the mocks' actions write into comes first, so it outlives them.
    Radio pdnRadio;
    Radio fdnRadio;
    std::vector<HttpRequest> requests;
    std::vector<std::string> screen;
    parameterizedCallbackFunction primaryPress = nullptr;
    void* primaryCtx = nullptr;
    parameterizedCallbackFunction secondaryPress = nullptr;
    void* secondaryCtx = nullptr;
    FakePlatformClock clock;
    PlayerProvider playerProvider;
    PdnDevice pdn;
    FdnRegistrationTests::FdnDevice fdn;
    uint8_t pdnMac[6] = {0x22, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t fdnMac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    NativeSerialDriver pdnOut{"pdn-out"};
    NativeSerialDriver pdnIn{"pdn-in"};
    NativeSerialDriver fdnIn{"fdn-in"};
    NativeSerialDriver fdnIn2{"fdn-in-2"};
    // After the devices and jacks: their destructors unhook both.
    RemoteDeviceCoordinator pdnRdc;
    RemoteDeviceCoordinator fdnRdc;
    FakeBandReader reader;
    FdnProvisioningManager* manager = nullptr;
    GameSession* session = nullptr;
    BandRegistration* bandRegistration = nullptr;
    PlayerFetchApp* app = nullptr;
    RegistrationView fdnView;
    bool fdnCabled = true;
    std::optional<std::array<uint8_t, 6>> forgedPeer;
    DeviceType forgedType = DeviceType::PDN;
    uint8_t forgedSeqId = 0;
    std::string savedGmockVerbose;
};

constexpr uint8_t OTHER_FDN_MAC[6] = {0x0F, 0x0D, 0x0E, 0x01, 0x02, 0x03};
constexpr uint8_t OTHER_PDN_MAC[6] = {0x33, 0x02, 0x03, 0x04, 0x05, 0x06};

// "Tap band" only while the FDN on the jack says this PDN holds its turn; told
// by each context the FDN sends, with no replug.
inline void bandRegistrationWaitingScreenFollowsTheFdnTurn(BandRegistrationTests* suite) {
    suite->fdnCabled = false;
    suite->step();
    EXPECT_TRUE(suite->shows("Please")) << "no FDN cabled";

    suite->fdnCabled = true;
    suite->advance(200);
    EXPECT_TRUE(suite->shows("Tap band"));

    suite->manager->setRegistrationScreenShown(false);
    suite->advance(100);
    EXPECT_TRUE(suite->shows("Please")) << "the FDN closed the turn";

    suite->manager->setRegistrationScreenShown(true);
    suite->advance(100);
    EXPECT_TRUE(suite->shows("Tap band"));

    suite->unplugFdn();
    EXPECT_TRUE(suite->shows("Please")) << "the FDN unplugged";
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::AWAIT_BAND);

    // A PDN peer's context whose bytes would read as an open turn is not an FDN's.
    suite->forgedPeer.emplace();
    memcpy(suite->forgedPeer->data(), OTHER_PDN_MAC, 6);
    suite->forgedType = DeviceType::PDN;
    suite->step();
    PdnConnectionContext context{};
    memset(&context.player, static_cast<int>(RegistrationTurn::OPEN), sizeof(context.player));
    context.seqId = ++suite->forgedSeqId;
    BandRegistrationTests::receive(suite->pdnRadio, OTHER_PDN_MAC, PktType::kPdnConnectionContext,
                                   BandRegistrationTests::bytesOf(context));
    suite->advance(100);
    EXPECT_TRUE(suite->shows("Please")) << "a PDN peer opened the turn";
}

// The whole path: a tap at the FDN, the key over the jack, the profile over the
// radio, Welcome; the PDN tells the FDN it registered, so the FDN does not give
// it the turn again once its result hold ends.
inline void bandRegistrationTapRegistersAndTheFdnLetsThePdnGo(BandRegistrationTests* suite) {
    suite->advance(200);
    ASSERT_TRUE(suite->shows("Tap band"));
    suite->tapAndAnswer(encodeTestPlayerWithKey(RECORD_KEY_SIZE));

    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::WELCOME);
    const Player* player = suite->playerProvider.getPlayer();
    EXPECT_STREQ(player->getServerPlayerName(), "Alex");
    EXPECT_EQ(player->getServerPlayerId(), makeTestUuid(TEST_PLAYER_ID));
    EXPECT_TRUE(suite->holdsKeyOf(0xA5));
    EXPECT_EQ(suite->lastRegisteredSentToFdn(), 1);

    // Seeded from the player's id, so another PDN registering Alex rolls the same.
    const int rolledHere = std::rand();
    Player elsewhere;
    elsewhere.applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID), "Alex", Team(), {});
    elsewhere.applyRngSeedFromServerPlayerId();
    EXPECT_EQ(std::rand(), rolledHere);

    suite->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS + 500);
    EXPECT_EQ(suite->fdnView.screen, RegistrationScreen::NONE) << "the FDN queued it again";
}

inline void bandRegistrationProfileBeforeKeyRegisters(BandRegistrationTests* suite) {
    suite->advance(200);
    suite->sendProfile(suite->fdnMac, "Ada");
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::FETCH_USER);
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "a profile was applied without its key";

    BandRegistrationTests::sendKey(suite->pdnOut, suite->fdnMac, 0x3C);
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::WELCOME);
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Ada");
    EXPECT_TRUE(suite->holdsKeyOf(0x3C));
}

// A short key frame is not a key; a profile and key in the same tick register.
inline void bandRegistrationShortKeyIgnoredAndBothPiecesInOneTick(BandRegistrationTests* suite) {
    suite->advance(200);
    deliverFrame(suite->pdnOut, applicationFrameBytes(
                                static_cast<uint8_t>(ApplicationFrameKind::RECORD_KEY),
                                suite->fdnMac, std::vector<uint8_t>(RECORD_KEY_SIZE - 1, 0x12)));
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::AWAIT_BAND);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld()) << "a short key was taken";

    suite->sendProfile(suite->fdnMac, "Ada");
    BandRegistrationTests::sendKey(suite->pdnOut, suite->fdnMac, 0x13);
    suite->step();
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::WELCOME);
    EXPECT_TRUE(suite->holdsKeyOf(0x13));
}

// A key frame names no sender: it counts only from the jack whose Connected
// peer is an FDN, and once a piece bound the attempt, only from that FDN's jack.
inline void bandRegistrationKeyOnlyFromTheConnectedFdnsJack(BandRegistrationTests* suite) {
    suite->advance(200);
    suite->cableForgedPeer(OTHER_PDN_MAC, DeviceType::PDN);
    BandRegistrationTests::sendKey(suite->pdnIn, OTHER_PDN_MAC, 0x11);
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::AWAIT_BAND);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld()) << "a PDN peer's key was taken";

    suite->cableForgedPeer(OTHER_FDN_MAC, DeviceType::FDN);
    suite->sendProfile(suite->fdnMac, "Ada");
    BandRegistrationTests::sendKey(suite->pdnIn, OTHER_FDN_MAC, 0x22);
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::FETCH_USER);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "a key from another FDN's jack joined the attempt";

    BandRegistrationTests::sendKey(suite->pdnOut, suite->fdnMac, 0x33);
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::WELCOME);
    EXPECT_TRUE(suite->holdsKeyOf(0x33));
}

// A profile counts only from the MAC of an FDN Connected on a jack.
inline void bandRegistrationProfileOnlyFromAConnectedFdn(BandRegistrationTests* suite) {
    suite->advance(200);
    suite->cableForgedPeer(OTHER_PDN_MAC, DeviceType::PDN);
    suite->sendProfile(OTHER_FDN_MAC, "Mallory");
    suite->sendProfile(OTHER_PDN_MAC, "Mallory");
    suite->sendProfile(OTHER_FDN_MAC, "Mallory", /*lookupFailed=*/true);
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::AWAIT_BAND);

    BandRegistrationTests::sendKey(suite->pdnOut, suite->fdnMac, 0x44);
    suite->step();
    suite->advance(USER_DATA_FETCH_TIMEOUT_MS + 100);
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "a stranger's profile was held";
}

// One piece and no other within 10 s fails the attempt and drops the piece at
// once; nothing arriving after that registers the PDN.
inline void bandRegistrationOnePieceTimesOutAndIsWiped(BandRegistrationTests* suite) {
    suite->advance(200);
    BandRegistrationTests::sendKey(suite->pdnOut, suite->fdnMac, 0x55);
    suite->step();
    ASSERT_EQ(suite->pdnState(), PlayerFetchStateId::FETCH_USER);
    ASSERT_TRUE(suite->holdsKeyOf(0x55));

    suite->advance(USER_DATA_FETCH_TIMEOUT_MS + 100);
    EXPECT_TRUE(suite->shows("Unable to"));
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    suite->sendProfile(suite->fdnMac, "Ada");
    suite->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 100);
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::AWAIT_BAND);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
}

// A lookup that issues no key: the FDN reports a failure, the PDN shows it,
// returns to the waiting screen and is told to tap again after the FDN's hold.
inline void
bandRegistrationLookupFailureShowsTheFailurePageThenWaits(BandRegistrationTests* suite) {
    suite->advance(200);
    suite->tapAndAnswer(encodeTestPlayerWithKey(0));
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::FETCH_USER);
    EXPECT_TRUE(suite->shows("Unable to"));

    suite->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 200);
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::AWAIT_BAND);
    EXPECT_TRUE(suite->shows("Tap band"));
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
}

// Once applied, a second profile or key from the same FDN changes nothing.
inline void bandRegistrationLateProfileAfterApplyIsIgnored(BandRegistrationTests* suite) {
    suite->advance(200);
    suite->tapAndAnswer(encodeTestPlayerWithKey(RECORD_KEY_SIZE));
    ASSERT_EQ(suite->pdnState(), PlayerFetchStateId::WELCOME);

    suite->sendProfile(suite->fdnMac, "Mallory");
    BandRegistrationTests::sendKey(suite->pdnOut, suite->fdnMac, 0x66);
    suite->step();
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Alex");
    EXPECT_TRUE(suite->holdsKeyOf(0xA5));
}

// The 5 s primary hold is the demo: role picker, Welcome, no key; the FDN is
// told the PDN has a profile and stops offering it the turn.
inline void bandRegistrationDemoHoldLeadsToChooseTeamThenWelcome(BandRegistrationTests* suite) {
    suite->advance(PLAYER_FETCH_DEMO_HOLD_MS);
    ON_CALL(*suite->pdn.mockPrimaryButton, longPressedMillis())
    .WillByDefault(Return(PLAYER_FETCH_DEMO_HOLD_MS - 1));
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::AWAIT_BAND);
    ON_CALL(*suite->pdn.mockPrimaryButton, longPressedMillis())
    .WillByDefault(Return(PLAYER_FETCH_DEMO_HOLD_MS));
    suite->step();
    ASSERT_EQ(suite->pdnState(), PlayerFetchStateId::CHOOSE_TEAM);
    ON_CALL(*suite->pdn.mockPrimaryButton, longPressedMillis()).WillByDefault(Return(0));

    BandRegistrationTests::sendKey(suite->pdnOut, suite->fdnMac, 0x77);
    suite->step();
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld()) << "a key reached the demo";

    ASSERT_NE(suite->secondaryPress, nullptr);
    suite->secondaryPress(suite->secondaryCtx);
    suite->step();
    EXPECT_EQ(suite->pdnState(), PlayerFetchStateId::WELCOME);
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Demo");
    EXPECT_TRUE(suite->playerProvider.getPlayer()->getServerPlayerId().isZero());
    EXPECT_EQ(suite->lastRegisteredSentToFdn(), 1);
    suite->advance(200);
    EXPECT_EQ(suite->fdnView.screen, RegistrationScreen::NONE);
}
