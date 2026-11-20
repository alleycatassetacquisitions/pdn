#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "alleycat-server/player-provision-codec.hpp"
#include "device-player-codec-tests.hpp"
#include "fake-band-reader.hpp"
#include "rdc-application-frame-tests.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>
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
        // As the ESP32 driver's disconnect(): giving Wi-Fi up fails, at once,
        // every request still waiting for its answer.
        ON_CALL(*device.mockHttpClient, setHttpClientState(_))
        .WillByDefault(testing::Invoke([this](HttpClientState state) {
            if (state != HttpClientState::DISCONNECTED) return;
            for (size_t i = 0; i < requests.size(); ++i) {
                if (i >= answered.size() || !answered[i]) {
                    fail(i, {WirelessError::CONNECTION_FAILED, "Disconnected before an answer"});
                }
            }
        }));
        device.serialManager->setInputJack(&jack1);
        device.serialManager->setSecondaryInputJack(&jack2);
        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
        manager = new FdnProvisioningManager(device.wirelessManager, &rdc, &reader);
        manager->setOnViewChanged([this](const RegistrationView& changed) {
            view = changed;
            views.push_back(changed);
        });
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
        sendContext(mac, registered);
        step();
    }

    /** The PDN @p mac sends its context over the radio, reporting @p registered. */
    void sendContext(const uint8_t* mac, bool registered) {
        PdnConnectionContext context{};
        context.seqId = ++contextSeqId;
        context.player.userId = RemoteDeviceCoordinator::PEER_USER_ID_NONE;
        context.player.registered = registered ? 1 : 0;
        const std::pair<PeerCommsInterface::PacketCallback, void*>& handler =
        packetHandlers[PktType::kPdnConnectionContext];
        handler.first(mac, reinterpret_cast<const uint8_t*>(&context), sizeof(context),
                      handler.second);
    }

    /**
     * Cables PDN_A on jack 1, taps for it and runs until its key is written and
     * its profile sent.
     */
    void sendKeyAndProfileToPdnA() {
        cable(jack1, PDN_A);
        tap();
        ASSERT_EQ(requests.size(), 1u);
        answer(0, encodeTestPlayerWithKey(RECORD_KEY_SIZE));
        step();
        step();
        ASSERT_EQ(provisionsTo(PDN_A).size(), 1u);
    }

    /** The two lines the FDN screen shows now. */
    std::pair<std::string, std::string> screenLines() const {
        const RegistrationScreenLines lines = getRegistrationScreenLines(view);
        return {lines.top, lines.bottom};
    }

    /** @return true if any view so far showed the registered result. */
    bool everShowedRegistered() const {
        return std::any_of(views.begin(), views.end(), [](const RegistrationView& shown) {
            return shown.screen == RegistrationScreen::RESULT &&
                   shown.outcome == RegistrationOutcome::REGISTERED;
        });
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
            const std::pair<PeerCommsInterface::SendStatusCallback, void*>& handler =
            sendStatusHandlers[frame.type];
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

    /** Taps a 4-byte band and runs the ticks that close the turn and start its lookup. */
    void tap() {
        reader.tapBand({0x04, 0xA2, 0x1B, 0x9C});
        step();
        step();
    }

    /** Answers request @p index with @p body, once. */
    void answer(size_t index, const std::string& body) {
        markAnswered(index);
        requests[index].onSuccess(body);
    }

    /** Fails request @p index with @p error, once. */
    void fail(size_t index, const WirelessErrorInfo& error) {
        markAnswered(index);
        requests[index].onError(error);
    }

    /** Marks request @p index as having had its one answer. */
    void markAnswered(size_t index) {
        if (answered.size() <= index) answered.resize(index + 1, false);
        answered[index] = true;
    }

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
    std::vector<RegistrationView> views;
    std::vector<CabledPdn> cabled;
    std::vector<SentFrame> sent;
    std::vector<SentFrame> unacked;
    bool radioLosesFrames = false;
    std::vector<bool> answered;
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

    suite->answer(0, encodeTestPlayerWithKey(RECORD_KEY_SIZE));
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
    EXPECT_EQ(packets[0].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
    EXPECT_EQ(memcmp(packets[0].playerId, makeTestUuid(TEST_PLAYER_ID).bytes, UUIDBytes::SIZE), 0);
    EXPECT_STREQ(packets[0].playerName, "Alex");
    EXPECT_TRUE(suite->provisionsTo(PDN_B).empty());
    EXPECT_FALSE(suite->everShowedRegistered()) << "WELCOME before the PDN confirmed";
}

// The FDN welcomes the player only once the PDN's resent context says its
// profile is applied, and says it waits until then, even for a PDN that
// applies it at the very end of its own attempt. The result holds for 3000 ms
// from the confirmation.
inline void fdnRegistrationWelcomesOnlyOnceThePdnConfirms(FdnRegistrationTests* suite) {
    using Lines = std::pair<std::string, std::string>;
    ASSERT_NO_FATAL_FAILURE(suite->sendKeyAndProfileToPdnA());
    EXPECT_EQ(suite->screenLines(), Lines("SENT", "WAITING"));
    suite->advance(REGISTRATION_ATTEMPT_MS);
    EXPECT_EQ(suite->screenLines(), Lines("SENT", "WAITING"));
    EXPECT_FALSE(suite->everShowedRegistered()) << "WELCOME before the PDN confirmed";

    suite->sendContext(PDN_A, /*registered=*/true);
    suite->step();
    EXPECT_EQ(suite->screenLines(), Lines("WELCOME", "Alex"));
    suite->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS - 100);
    EXPECT_EQ(suite->screenLines(), Lines("WELCOME", "Alex")) << "the hold ran from the profile";
    suite->advance(200);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::NONE);
}

// Only the PDN the key went to confirms, on its own jack: a registered context
// from the PDN on the other jack or from a MAC on no jack, or an unregistered
// one from that PDN, leaves the FDN waiting.
inline void fdnRegistrationCountsOnlyTheConfirmOfThePdnOnItsJack(FdnRegistrationTests* suite) {
    using Lines = std::pair<std::string, std::string>;
    ASSERT_NO_FATAL_FAILURE(suite->sendKeyAndProfileToPdnA());
    suite->cable(suite->jack2, PDN_B, /*registered=*/true);
    suite->sendContext(PDN_C, /*registered=*/true);
    suite->step();
    suite->sendContext(PDN_A, /*registered=*/false);
    suite->step();
    EXPECT_EQ(suite->screenLines(), Lines("SENT", "WAITING"));
    EXPECT_FALSE(suite->everShowedRegistered()) << "another PDN's context confirmed PDN A";

    suite->sendContext(PDN_A, /*registered=*/true);
    suite->step();
    EXPECT_EQ(suite->screenLines(), Lines("WELCOME", "Alex"));
}

// The PDN is unplugged after its profile went out but before it confirmed: the
// FDN says it left, never WELCOME.
inline void fdnRegistrationShowsPdnLeftWhenUnpluggedBeforeConfirming(FdnRegistrationTests* suite) {
    using Lines = std::pair<std::string, std::string>;
    ASSERT_NO_FATAL_FAILURE(suite->sendKeyAndProfileToPdnA());
    suite->unplug(PDN_A);
    EXPECT_EQ(suite->screenLines(), Lines("PDN LEFT", "JACK 1"));
    EXPECT_FALSE(suite->everShowedRegistered()) << "WELCOME for a PDN that left unconfirmed";
}

// Another PDN takes the jack before the first confirmed and reports a profile
// applied: that is not the confirmation.
inline void fdnRegistrationShowsPdnLeftWhenItsJackChangesPeer(FdnRegistrationTests* suite) {
    using Lines = std::pair<std::string, std::string>;
    ASSERT_NO_FATAL_FAILURE(suite->sendKeyAndProfileToPdnA());
    suite->cabled.clear();
    suite->cable(suite->jack1, PDN_C, /*registered=*/true);
    EXPECT_EQ(suite->screenLines(), Lines("PDN LEFT", "JACK 1"));
    EXPECT_FALSE(suite->everShowedRegistered()) << "the new peer's context confirmed PDN A";
}

// No confirmation by the deadline: the FDN says it could not confirm and sends
// no failure, which could land in the next attempt of a PDN whose own shorter
// attempt has already ended; a late confirmation changes nothing.
inline void fdnRegistrationGivesUpOnAConfirmationThatNeverComes(FdnRegistrationTests* suite) {
    using Lines = std::pair<std::string, std::string>;
    ASSERT_NO_FATAL_FAILURE(suite->sendKeyAndProfileToPdnA());
    suite->advance(FdnProvisioningManager::CONFIRM_TIMEOUT_MS - 100);
    EXPECT_EQ(suite->screenLines(), Lines("SENT", "WAITING"));
    suite->advance(200);
    EXPECT_EQ(suite->screenLines(), Lines("UNABLE TO", "CONFIRM"));
    EXPECT_EQ(suite->provisionsTo(PDN_A).size(), 1u) << "a failure after the PDN gave up";

    suite->sendContext(PDN_A, /*registered=*/true);
    suite->step();
    EXPECT_EQ(suite->screenLines(), Lines("UNABLE TO", "CONFIRM"));
    EXPECT_FALSE(suite->everShowedRegistered()) << "WELCOME after the deadline";
}

// An answer with no usable key, or no player, registers nobody: no key frame,
// no profile, only the failure for the PDN still cabled.
inline void fdnRegistrationWithoutKeySendsNoKeyAndNoProfile(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->answer(0, encodeTestPlayerWithKey(RECORD_KEY_SIZE - 1));
    suite->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS + 40);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::TAP_BAND);

    suite->tap();
    ASSERT_EQ(suite->requests.size(), 2u);
    suite->fail(1, {WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    suite->step();
    EXPECT_EQ(suite->view.outcome, RegistrationOutcome::NO_PLAYER);

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

    suite->answer(0, encodeTestPlayerWithKey(RECORD_KEY_SIZE));
    suite->step();
    suite->step();
    EXPECT_TRUE(suite->keyFrames1.empty()) << "a key went to the PDN now on the jack";
    EXPECT_TRUE(suite->provisionsTo(PDN_A).empty());
    EXPECT_TRUE(suite->provisionsTo(PDN_C).empty());
    EXPECT_EQ(suite->view.outcome, RegistrationOutcome::PDN_LEFT);
}

// The PDN is swapped for another after its key frame went out but before the
// FDN collected the outcome: the frame reports written, yet no profile follows.
inline void
fdnRegistrationSendsNoProfileToAPdnSwappedAsItsKeyIsWritten(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->answer(0, encodeTestPlayerWithKey(RECORD_KEY_SIZE));
    suite->emitAndCollect();
    ASSERT_EQ(suite->keyFrames1.size(), 1u);

    suite->cabled.clear();
    suite->cable(suite->jack1, PDN_C);
    suite->step();
    EXPECT_TRUE(suite->provisionsTo(PDN_A).empty()) << "profile sent to a PDN that left";
    EXPECT_TRUE(suite->provisionsTo(PDN_C).empty());
    EXPECT_EQ(suite->view.outcome, RegistrationOutcome::PDN_LEFT);
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
    suite->fail(0, {WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    suite->advance(1000);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
    suite->tap();
    EXPECT_EQ(suite->requests.size(), 1u) << "a tap during the hold was looked up";

    suite->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::OPEN);
    suite->tap();
    EXPECT_EQ(suite->requests.size(), 2u);
}

// A PDN whose lookup failed rejoins at the back, behind a PDN cabled during it.
inline void fdnRegistrationRequeuesAFailedPdnAtTheBack(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    suite->cable(suite->jack2, PDN_B);
    suite->fail(0, {WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    suite->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS + 40);
    EXPECT_EQ(suite->lastTurnTo(PDN_B), RegistrationTurn::OPEN);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
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

// A turn nobody taps for passes to the next PDN 30 s after it started waiting,
// however long the head held the turn alone before it came.
inline void fdnRegistrationPassesAnUntappedTurnOn(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->advance(2 * FdnProvisioningManager::REGISTRATION_PENDING_MS - 100);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::OPEN) << "a lone PDN lost the turn";
    suite->cable(suite->jack2, PDN_B);
    suite->advance(200);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::OPEN) << "a late arrival cut the turn";
    suite->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
    EXPECT_EQ(suite->lastTurnTo(PDN_B), RegistrationTurn::OPEN);
}

// The tapped PDN must hear its turn closed before Wi-Fi takes the radio: the
// lookup waits for that context to be delivered, or TURN_CLOSE_MS at most.
inline void fdnRegistrationClosesTheTurnBeforeTheLookupTakesTheRadio(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->radioLosesFrames = true;
    suite->reader.tapBand({0x04, 0xA2, 0x1B, 0x9C});
    suite->step();
    suite->step();
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
    EXPECT_TRUE(suite->requests.empty()) << "the lookup took the radio before the turn closed";
    suite->advance(FdnProvisioningManager::TURN_CLOSE_MS);
    EXPECT_EQ(suite->requests.size(), 1u) << "a lost turn context held the lookup forever";
}

// A modified PDN on the other jack claims the MAC of the PDN waiting on jack 1:
// a context told to that MAC could be tapped for either jack, so no turn opens,
// the screen does not ask for a tap, and no key can reach the impostor's jack.
inline void fdnRegistrationOpensNoTurnForAMacOnBothJacks(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    ASSERT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::OPEN);
    suite->cable(suite->jack2, PDN_A);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::NONE) << "TAP BAND with no turn open";

    suite->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 40);
    EXPECT_EQ(suite->lastTurnTo(PDN_A), RegistrationTurn::CLOSED) << "the rotation opened a turn";
    suite->tap();
    EXPECT_TRUE(suite->requests.empty()) << "a tap was looked up with the MAC on both jacks";
    EXPECT_TRUE(suite->keyFrames2.empty());
}

// A lookup the server never answers ends at its deadline with the radio back
// on ESP-NOW. Giving Wi-Fi up fails the request at once, and that failure ends
// nothing twice: the PDN hears of the failure once.
inline void fdnRegistrationLookupWithoutAnswerTimesOut(FdnRegistrationTests* suite) {
    suite->cable(suite->jack1, PDN_A);
    suite->tap();
    ASSERT_EQ(suite->requests.size(), 1u);
    suite->advance(FdnProvisioningManager::LOOKUP_TIMEOUT_MS + 40);
    EXPECT_EQ(suite->view.screen, RegistrationScreen::RESULT);
    EXPECT_EQ(suite->view.outcome, RegistrationOutcome::LOOKUP_FAILED);
    EXPECT_EQ(suite->device.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);
    EXPECT_EQ(suite->provisionsTo(PDN_A).size(), 1u);
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
    strcpy(view.playerName, "Ada");
    const std::vector<std::pair<RegistrationOutcome, Lines>> outcomes = {
    {RegistrationOutcome::REGISTERED, {"WELCOME", "Ada"}},
    {RegistrationOutcome::PDN_LEFT, {"PDN LEFT", "JACK 1"}},
    {RegistrationOutcome::KEY_NOT_ISSUED, {"UNABLE TO", "ISSUE KEY"}},
    {RegistrationOutcome::NO_PLAYER, {"SEE DESK", "JACK 1"}},
    {RegistrationOutcome::SERVER_NOT_TRUSTED, {"SERVER", "NOT TRUSTED"}},
    {RegistrationOutcome::LOOKUP_FAILED, {"UNABLE TO", "LOCATE ASSET"}},
    };
    for (const auto& outcome : outcomes) {
        view.outcome = outcome.first;
        EXPECT_EQ(lines(), outcome.second);
    }
}

// The profile packet carries the registration fields, at most three affiliations.
inline void playerProvisionCodecEncodesTheRegistrationFields() {
    Player player;
    Team team;
    team.id = makeTestUuid(TEST_PLAYER_ID);
    strcpy(team.name, "Hunter");
    std::vector<Affiliation> affiliations(4);
    for (size_t i = 0; i < affiliations.size(); ++i) {
        affiliations[i].affiliationId.bytes[0] = static_cast<uint8_t>(i + 11);
        snprintf(affiliations[i].affiliationName, PlayerLimits::NAME_BUFFER_SIZE, "Helix %zu", i);
    }
    player.applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID), "Ada", team, affiliations);

    const PlayerProvisionPacket packet = PlayerProvisionCodec::encode(player);
    EXPECT_EQ(packet.status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
    EXPECT_EQ(memcmp(packet.playerId, makeTestUuid(TEST_PLAYER_ID).bytes, UUIDBytes::SIZE), 0);
    EXPECT_STREQ(packet.playerName, "Ada");
    EXPECT_EQ(memcmp(packet.teamId, team.id.bytes, UUIDBytes::SIZE), 0);
    EXPECT_STREQ(packet.teamName, "Hunter");
    ASSERT_EQ(packet.affiliationCount, PlayerLimits::MAX_AFFILIATIONS);
    EXPECT_EQ(packet.affiliations[2].affiliationId[0], 13);
    EXPECT_STREQ(packet.affiliations[2].affiliationName, "Helix 2");
}
