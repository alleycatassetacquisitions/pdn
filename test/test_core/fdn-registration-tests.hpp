#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "alleycat-server/player-provision-codec.hpp"
#include "device-player-codec-tests.hpp"
#include "fake-band-reader.hpp"
#include "rdc-application-frame-tests.hpp"

#include <array>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

// ============================================
// FDN registration by band tap
// ============================================
//
// A real coordinator running as an FDN with both input jacks, its radio mocked:
// a test cables PDNs by HELLO and their context, taps bands on a fake reader,
// answers the lookup's HTTP request by hand and reads back what went over each
// jack and over the radio.

constexpr uint8_t PDN_A[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
constexpr uint8_t PDN_B[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};
constexpr uint8_t PDN_C[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};

class FdnRegistrationTests : public testing::Test {
public:
    /** Runs the coordinator as an FDN. */
    class FdnDevice : public MockDevice {
    public:
        /** @return DeviceType::FDN. */
        DeviceType getDeviceType() override { return DeviceType::FDN; }
    };

    /** One radio frame the FDN sent. */
    struct SentFrame {
        PktType type;
        std::array<uint8_t, 6> to;
        std::vector<uint8_t> data;
    };

    /** A PDN on a jack, kept alive by its HELLO until unplugged. */
    struct CabledPdn {
        NativeSerialDriver* jack;
        const uint8_t* mac;
    };

    /** Wires the radio mock, brings the coordinator up and builds the manager. */
    void SetUp() override {
        clock.setTime(1000);
        SimpleTimer::setPlatformClock(&clock);
        wireRadioDefaults(device, fdnMac);
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _))
        .WillByDefault(
        testing::Invoke([this](const uint8_t* to, PktType type, const uint8_t* data, size_t len) {
            SentFrame frame{type, {}, std::vector<uint8_t>(data, data + len)};
            memcpy(frame.to.data(), to, 6);
            sent.push_back(frame);
            unacked.push_back(frame);
            return 1;
        }));
        ON_CALL(*device.mockPeerComms, setPacketHandler(_, _, _))
        .WillByDefault(
        testing::Invoke([this](PktType type, PeerCommsInterface::PacketCallback callback,
                               void* ctx) { packetHandlers[type] = {std::move(callback), ctx}; }));
        ON_CALL(*device.mockPeerComms, setSendStatusHandler(_, _, _))
        .WillByDefault(testing::Invoke(
        [this](PktType type, PeerCommsInterface::SendStatusCallback callback, void* ctx) {
            sendStatusHandlers[type] = {std::move(callback), ctx};
        }));
        ON_CALL(*device.mockHttpClient, queueRequest(_))
        .WillByDefault(testing::Invoke([this](HttpRequest&& request) {
            requests.push_back(std::move(request));
            return true;
        }));
        device.serialManager->setInputJack(&jack1);
        device.serialManager->setSecondaryInputJack(&jack2);
        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
        manager = new FdnProvisioningManager(device.wirelessManager, &rdc, &reader);
        manager->setOnViewChanged([this](const RegistrationView& changed) { view = changed; });
        manager->setRegistrationScreenShown(true);
    }

    /** Deletes the manager before the coordinator it listens to. */
    void TearDown() override {
        delete manager;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /** Cables a PDN reporting @p registered on @p jack and runs one tick. */
    void cable(NativeSerialDriver& jack, const uint8_t* mac, bool registered = false) {
        cabled.push_back({&jack, mac});
        deliverFrame(jack, pdnHello(mac));
        rdc.sync(&device);
        PdnConnectionContext context{};
        context.seqId = ++contextSeqId;
        context.player.userId = RemoteDeviceCoordinator::PEER_USER_ID_NONE;
        context.player.registered = registered ? 1 : 0;
        const auto& handler = packetHandlers[PktType::kPdnConnectionContext];
        handler.first(mac, reinterpret_cast<const uint8_t*>(&context), sizeof(context),
                      handler.second);
        step();
    }

    /** Stops @p mac's HELLO and waits out the silent-link window. */
    void unplug(const uint8_t* mac) {
        for (size_t i = 0; i < cabled.size(); ++i) {
            if (cabled[i].mac == mac) cabled.erase(cabled.begin() + i);
        }
        advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 40);
    }

    /**
     * One tick of @p ms: cabled PDNs say HELLO, the FDN emits (its key frames
     * collected per jack), the coordinator and manager sync, and the radio
     * reports every frame delivered unless `radioLosesFrames`.
     */
    void step(unsigned long ms = 20) {
        clock.advance(ms);
        for (const CabledPdn& pdn : cabled)
            deliverFrame(*pdn.jack, pdnHello(pdn.mac));
        emitAndCollect();
        rdc.sync(&device);
        manager->sync();
        std::vector<SentFrame> settling;
        settling.swap(unacked);
        for (const SentFrame& frame : settling) {
            const auto& handler = sendStatusHandlers[frame.type];
            if (handler.first && !radioLosesFrames) {
                handler.first(frame.to.data(), frame.data.data(), frame.data.size(), true,
                              handler.second);
            }
        }
    }

    /** Steps through @p ms. */
    void advance(unsigned long ms) {
        for (unsigned long elapsed = 0; elapsed < ms; elapsed += 20)
            step();
    }

    /** Emits one HELLO pass and keeps each jack's key frames. */
    void emitAndCollect() {
        rdc.emitHello();
        for (std::pair<NativeSerialDriver*, std::vector<std::vector<uint8_t>>*> jack :
             {std::make_pair(&jack1, &keyFrames1), std::make_pair(&jack2, &keyFrames2)}) {
            DecodedJackOutput output = decodeJackOutput(*jack.first);
            for (size_t i = 0; i < output.headers.size(); ++i) {
                EXPECT_EQ(output.headers[i].kind,
                          static_cast<uint8_t>(ApplicationFrameKind::RECORD_KEY));
                jack.second->push_back(output.payloads[i]);
            }
        }
    }

    /** The turn the last context sent to @p mac carried; CLOSED if none went. */
    RegistrationTurn lastTurnTo(const uint8_t* mac) const {
        RegistrationTurn turn = RegistrationTurn::CLOSED;
        for (const SentFrame& frame : sent) {
            if (frame.type != PktType::kFdnConnectionContext || memcmp(frame.to.data(), mac, 6))
                continue;
            FdnConnectionContext context{};
            memcpy(&context, frame.data.data(), sizeof(context));
            turn = context.fdn.registrationTurn;
        }
        return turn;
    }

    /** Every provision packet sent to @p mac, in order. */
    std::vector<PlayerProvisionPacket> provisionsTo(const uint8_t* mac) const {
        std::vector<PlayerProvisionPacket> packets;
        for (const SentFrame& frame : sent) {
            if (frame.type != PktType::kPlayerProvision || memcmp(frame.to.data(), mac, 6))
                continue;
            PlayerProvisionPacket packet{};
            memcpy(&packet, frame.data.data(), sizeof(packet));
            packets.push_back(packet);
        }
        return packets;
    }

    /** Taps a 4-byte band. */
    void tap() { reader.tapBand({0x04, 0xA2, 0x1B, 0x9C}); }

    /** A HELLO from the PDN @p mac. */
    static std::vector<uint8_t> pdnHello(const uint8_t* mac) {
        HelloPayload hello{};
        memcpy(hello.source, mac, 6);
        hello.deviceType = static_cast<uint8_t>(DeviceType::PDN);
        return encodeFramed(hello);
    }

    FakePlatformClock clock;
    FdnDevice device;
    uint8_t fdnMac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    NativeSerialDriver jack1{"fdn-in"};
    NativeSerialDriver jack2{"fdn-in-2"};
    // After the jacks: its destructor unhooks them.
    RemoteDeviceCoordinator rdc;
    FakeBandReader reader;
    FdnProvisioningManager* manager = nullptr;
    RegistrationView view;
    std::vector<CabledPdn> cabled;
    std::vector<SentFrame> sent;
    std::vector<SentFrame> unacked;
    bool radioLosesFrames = false;
    std::vector<HttpRequest> requests;
    std::vector<std::vector<uint8_t>> keyFrames1;
    std::vector<std::vector<uint8_t>> keyFrames2;
    std::map<PktType, std::pair<PeerCommsInterface::PacketCallback, void*>> packetHandlers;
    std::map<PktType, std::pair<PeerCommsInterface::SendStatusCallback, void*>> sendStatusHandlers;
    uint8_t contextSeqId = 0;
};

// Two unregistered PDNs: only the first one's PDN is told to tap, and a
// registered PDN is never queued.
inline void fdnRegistrationTurnIsOnlyWithTheFirstPdn(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->cable(suite->jack2, PDN_B);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::OPEN);
    EXPECT_EQ(suite->lastTurnTo(PDN_B), RegistrationTurn::CLOSED);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::TAP_BAND);
    EXPECT_EQ(suite->view.jack, SerialIdentifier::INPUT_JACK);

    suite->unplug(PDN_A);
    suite->cable(suite->jack1, PDN_C, /*registered=*/true);
    EXPECT_EQ(suite->lastTurnTo(PDN_B), RegistrationTurn::OPEN) << "the turn did not move on";
    EXPECT_EQ(suite->lastTurnTo(PDN_C), RegistrationTurn::CLOSED);
    EXPECT_EQ(suite->view.jack, SerialIdentifier::INPUT_JACK_SECONDARY);

    suite->unplug(PDN_B);
    EXPECT_EQ(suite->lastTurnTo(PDN_C), RegistrationTurn::CLOSED) << "a registered PDN got a turn";
    EXPECT_EQ(suite->view.screen, RegistrationScreen::NONE);
}

// The tap registers the PDN holding the turn and nobody else: its key goes over
// its jack, and only once that frame is written does its profile go out.
inline void fdnRegistrationTapSendsKeyThenProfileToTheTurnsPdn(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->cable(suite->jack2, PDN_B);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->step();
    EXPECT_EQ(suite->view.screen, RegistrationScreen::LOOKING_UP);

    suite->requests[0].onSuccess(encodeTestPlayerWithKey(RECORD_KEY_SIZE));
    EXPECT_EQ(suite->device.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW)
    << "the radio was not handed back to ESP-NOW";
    suite->emitAndCollect();
    ASSERT_EQ(suite->keyFrames1.size(), 1u);
    EXPECT_EQ(suite->keyFrames1[0], std::vector<uint8_t>(RECORD_KEY_SIZE, 0xA5));
    EXPECT_TRUE(suite->keyFrames2.empty());
    EXPECT_TRUE(suite->provisionsTo(PDN_A).empty()) << "profile sent before the key was written";

    suite->step();
    const std::vector<PlayerProvisionPacket> packets = suite->provisionsTo(PDN_A);
    ASSERT_EQ(packets.size(), 1u);
    Player delivered;
    ASSERT_TRUE(PlayerProvisionCodec::decode(packets[0], delivered));
    EXPECT_EQ(delivered.getServerPlayerId(), makeTestUuid(TEST_PLAYER_ID));
    EXPECT_STREQ(delivered.getServerPlayerName(), "Alex");
    EXPECT_TRUE(suite->provisionsTo(PDN_B).empty());
    EXPECT_EQ(suite->view.screen, RegistrationScreen::RESULT);
    EXPECT_TRUE(suite->view.registered);
    EXPECT_STREQ(suite->view.playerName, "Alex");
}

// An answer with no usable key, or no player, registers nobody: no key frame,
// no profile, only the failure for the PDN still cabled.
inline void fdnRegistrationWithoutKeySendsNoKeyAndNoProfile(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->requests[0].onSuccess(encodeTestPlayerWithKey(RECORD_KEY_SIZE - 1));
    suite->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS + 40);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::TAP_BAND);

    suite->tap();
    ASSERT_EQ(suite->requests.size(), 2u);
    suite->requests[1].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    suite->step();
    EXPECT_EQ(suite->view.result, ProvisionLookupResult::NO_PLAYER);

    EXPECT_TRUE(suite->keyFrames1.empty());
    const std::vector<PlayerProvisionPacket> packets = suite->provisionsTo(PDN_A);
    ASSERT_EQ(packets.size(), 2u);
    for (const PlayerProvisionPacket& packet : packets) {
        EXPECT_EQ(packet.status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));
    }
}

// A PDN that leaves during the lookup gets nothing, and neither does the PDN
// plugged into its jack before the answer.
inline void fdnRegistrationSendsNothingToAPdnThatLeftDuringLookup(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->unplug(PDN_A);
    suite->cable(suite->jack1, PDN_C);

    suite->requests[0].onSuccess(encodeTestPlayerWithKey(RECORD_KEY_SIZE));
    suite->step();
    suite->step();
    EXPECT_TRUE(suite->keyFrames1.empty()) << "a key went to the PDN now on the jack";
    EXPECT_TRUE(suite->provisionsTo(PDN_A).empty());
    EXPECT_TRUE(suite->provisionsTo(PDN_C).empty());
    EXPECT_EQ(suite->view.result, ProvisionLookupResult::KEY_ISSUED);
    EXPECT_FALSE(suite->view.registered);
}

// The PDN is swapped for another after its key frame went out but before the
// FDN collected the outcome: the frame reports written, yet no profile follows.
inline void
fdnRegistrationSendsNoProfileToAPdnSwappedAsItsKeyIsWritten(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->requests[0].onSuccess(encodeTestPlayerWithKey(RECORD_KEY_SIZE));
    suite->emitAndCollect();
    ASSERT_EQ(suite->keyFrames1.size(), 1u);

    suite->cabled.clear();
    suite->cable(suite->jack1, PDN_C);
    suite->step();
    EXPECT_TRUE(suite->provisionsTo(PDN_A).empty()) << "profile sent to a PDN that left";
    EXPECT_TRUE(suite->provisionsTo(PDN_C).empty());
    EXPECT_FALSE(suite->view.registered);
}

// A UID of no anticollision length is not looked up and the turn stays. Taps
// during the result hold are ignored and no turn is open; afterwards the PDN
// still waiting gets its turn back.
inline void fdnRegistrationIgnoresTapsDuringTheResultHold(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->reader.tapBand({0x04, 0xA2, 0x1B});
    suite->step();
    EXPECT_TRUE(suite->requests.empty());
    EXPECT_EQ(suite->view.screen, RegistrationScreen::TAP_BAND);

    suite->tap();
    suite->requests[0].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    suite->advance(1000);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
    suite->tap();
    EXPECT_EQ(suite->requests.size(), 1u) << "a tap during the hold was looked up";

    suite->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::OPEN);
    suite->tap();
    EXPECT_EQ(suite->requests.size(), 2u);
}

// Turns are open only while the registration screen is shown and the reader
// answers; with no reader the screen says why.
inline void fdnRegistrationTurnNeverOpensWithoutAReader(FdnRegistrationTests* suite) {
    suite->manager->setRegistrationScreenShown(false);
    suite->cable(suite->jack1, PDN_A);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED) << "turn open off the screen";

    suite->reader.setResponding(false);
    suite->manager->setRegistrationScreenShown(true);
    suite->advance(200);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::NO_READER);

    suite->reader.setResponding(true);
    suite->step();
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::OPEN);
}

// The context carrying the turn's move is lost: once the radio gives it up,
// the FDN sends that PDN a fresh context with the turn.
inline void fdnRegistrationResendsTheTurnAfterAnAbandonedContext(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->cable(suite->jack2, PDN_B);
    suite->radioLosesFrames = true;
    suite->unplug(PDN_A);
    ASSERT_EQ(suite->lastTurnTo(PDN_B), RegistrationTurn::OPEN);
    uint8_t movedSeqId = 0;
    for (const FdnRegistrationTests::SentFrame& frame : suite->sent) {
        if (frame.type == PktType::kFdnConnectionContext && memcmp(frame.to.data(), PDN_B, 6) == 0)
            movedSeqId = frame.data[0];
    }

    suite->advance(Resender::STALE_AFTER_MS + 200);
    bool resent = false;
    for (const FdnRegistrationTests::SentFrame& frame : suite->sent) {
        if (frame.type != PktType::kFdnConnectionContext || memcmp(frame.to.data(), PDN_B, 6))
            continue;
        FdnConnectionContext context{};
        memcpy(&context, frame.data.data(), sizeof(context));
        if (context.seqId != movedSeqId && context.fdn.registrationTurn == RegistrationTurn::OPEN)
            resent = true;
    }
    EXPECT_TRUE(resent) << "the abandoned turn context was never sent again";
}

// A turn nobody taps for passes to the next PDN.
inline void fdnRegistrationPassesAnUntappedTurnOn(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->cable(suite->jack2, PDN_B);
    suite->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 40);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
    EXPECT_EQ(suite->lastTurnTo(PDN_B), RegistrationTurn::OPEN);
}

// A lookup the server never answers ends at its deadline with the radio back
// on ESP-NOW, and its late answer hands out no key.
inline void fdnRegistrationLookupWithoutAnswerTimesOut(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->advance(FdnProvisioningManager::LOOKUP_TIMEOUT_MS + 40);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::RESULT);
    EXPECT_EQ(suite->view.result, ProvisionLookupResult::LOOKUP_FAILED);
    EXPECT_EQ(suite->device.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);

    suite->requests[0].onSuccess(encodeTestPlayerWithKey(RECORD_KEY_SIZE));
    suite->step();
    EXPECT_TRUE(suite->keyFrames1.empty());
}

// #452: what each registration view puts on the FDN screen.
inline void fdnRegistrationScreenText() {
    RegistrationView view;
    auto lines = [&view]() {
        const RegistrationScreenLines got = getRegistrationScreenLines(view);
        return std::make_pair(std::string(got.top), std::string(got.bottom));
    };
    using Lines = std::pair<std::string, std::string>;
    view.screen = RegistrationScreen::TAP_BAND;
    view.jack = SerialIdentifier::INPUT_JACK_SECONDARY;
    EXPECT_EQ(lines(), Lines("TAP BAND", "JACK 2"));
    view.screen = RegistrationScreen::NO_READER;
    view.jack = SerialIdentifier::INPUT_JACK;
    EXPECT_EQ(lines(), Lines("NO READER", "JACK 1"));
    view.screen = RegistrationScreen::RESULT;
    view.registered = true;
    strcpy(view.playerName, "Ada");
    EXPECT_EQ(lines(), Lines("WELCOME", "Ada"));
    view.registered = false;
    const std::vector<std::pair<ProvisionLookupResult, Lines>> results = {
    {ProvisionLookupResult::KEY_ISSUED, {"PDN LEFT", "JACK 1"}},
    {ProvisionLookupResult::KEY_NOT_ISSUED, {"UNABLE TO", "ISSUE KEY"}},
    {ProvisionLookupResult::NO_PLAYER, {"SEE DESK", "JACK 1"}},
    {ProvisionLookupResult::SERVER_NOT_TRUSTED, {"SERVER", "NOT TRUSTED"}},
    {ProvisionLookupResult::LOOKUP_FAILED, {"UNABLE TO", "LOCATE ASSET"}},
    };
    for (const auto& result : results) {
        view.result = result.first;
        EXPECT_EQ(lines(), result.second);
    }
}

// The profile packet carries the registration fields across, and a decoding
// PDN never trusts the count or the names off the radio.
inline void playerProvisionCodecRoundTripsAndBoundsTheWire() {
    Player player;
    Team team;
    team.id = makeTestUuid(TEST_PLAYER_ID);
    strcpy(team.name, "Hunter");
    std::vector<Affiliation> affiliations(4);
    for (size_t i = 0; i < affiliations.size(); ++i) {
        affiliations[i].categoryId.bytes[0] = static_cast<uint8_t>(i + 1);
        affiliations[i].affiliationId.bytes[0] = static_cast<uint8_t>(i + 11);
        snprintf(affiliations[i].affiliationName, PlayerLimits::NAME_BUFFER_SIZE, "Helix %zu", i);
    }
    player.applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID), "Ada", team, affiliations);

    PlayerProvisionPacket packet = PlayerProvisionCodec::encode(player);
    Player decoded;
    ASSERT_TRUE(PlayerProvisionCodec::decode(packet, decoded));
    EXPECT_EQ(decoded.getServerPlayerId(), makeTestUuid(TEST_PLAYER_ID));
    EXPECT_STREQ(decoded.getServerPlayerName(), "Ada");
    EXPECT_STREQ(decoded.getRegistrationTeam().name, "Hunter");
    ASSERT_EQ(decoded.getRegistrationAffiliations().size(), PlayerLimits::MAX_AFFILIATIONS);
    EXPECT_STREQ(decoded.getRegistrationAffiliations()[2].affiliationName, "Helix 2");

    packet.affiliationCount = 200;
    memset(packet.playerName, 'x', sizeof(packet.playerName));
    ASSERT_TRUE(PlayerProvisionCodec::decode(packet, decoded));
    EXPECT_EQ(decoded.getRegistrationAffiliations().size(), PlayerLimits::MAX_AFFILIATIONS);
    EXPECT_EQ(strlen(decoded.getServerPlayerName()), PlayerLimits::NAME_BUFFER_SIZE - 1);

    Player untouched;
    PlayerProvisionPacket failed = PlayerProvisionCodec::encode(player);
    failed.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
    EXPECT_FALSE(PlayerProvisionCodec::decode(failed, untouched));
    PlayerProvisionPacket nilPlayer = PlayerProvisionCodec::encode(Player());
    strcpy(nilPlayer.playerName, "Ada");
    EXPECT_FALSE(PlayerProvisionCodec::decode(nilPlayer, untouched));
    EXPECT_FALSE(untouched.hasRegistrationProfile());
}
