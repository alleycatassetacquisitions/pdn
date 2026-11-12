#if defined(NATIVE_BUILD) && defined(PERF_BUILD)

/**
 * Match Performance Harness
 *
 * Runs a large volume of complete hunter-vs-bounty match cycles using only the
 * business logic layer. No GoogleTest, no GMock, no rendering, no I/O in the hot
 * path. Stubs satisfy every interface with no-op or minimal implementations.
 *
 * Build:  pio run -e native_perf
 * Run:    .pio/build/native_perf/program [NUM_MATCHES]
 * Profile:
 *   valgrind --tool=callgrind --callgrind-out-file=callgrind.out \
 *     .pio/build/native_perf/program 50000
 *   callgrind_annotate --auto=yes callgrind.out > callgrind_report.txt
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <chrono>
#include <functional>
#include <string>
#include <random>

#include "device/drivers/logger.hpp"
#include "device/drivers/platform-clock.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "id-generator.hpp"
#include "game/player.hpp"
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "wireless/match-packet.hpp"

// ============================================================
// Null logger — suppresses all LOG_* output in the hot path
// ============================================================

class NullLogger : public LoggerInterface {
public:
    void vlog(LogLevel, const char*, const char*, int,
              const char*, va_list) override {}
};

// ============================================================
// Controllable fake clock — no syscall overhead
// ============================================================

class StepClock : public PlatformClock {
public:
    unsigned long milliseconds() override { return time_ms; }
    void set(unsigned long t) { time_ms = t; }
    void advance(unsigned long delta) { time_ms += delta; }
    unsigned long time_ms = 0;
};

// ============================================================
// Minimal no-op stubs for peripheral interfaces
// ============================================================

class StubPeerComms : public PeerCommsInterface {
public:
    int sendData(const uint8_t*, PktType,
                 const uint8_t*, const size_t) override { return 1; }
    void setPacketHandler(PktType, PacketCallback cb, void* ctx) override {
        handlerCb_ = cb;
        handlerCtx_ = ctx;
    }
    void clearPacketHandler(PktType) override {
        handlerCb_ = nullptr;
        handlerCtx_ = nullptr;
    }

    void deliver(PktType, const uint8_t* src, const uint8_t* data, size_t len) {
        if (handlerCb_ != nullptr) {
            handlerCb_(src, data, len, handlerCtx_);
        }
    }
    const uint8_t* getGlobalBroadcastAddress() override { return broadcast_; }
    uint8_t* getMacAddress() override { return mac_; }
    void setPeerCommsState(PeerCommsState) override {}
    PeerCommsState getPeerCommsState() override { return PeerCommsState::ENABLED; }
    void enable() override {}
    void disable() override {}

private:
    PacketCallback handlerCb_ = nullptr;
    void* handlerCtx_ = nullptr;
    uint8_t mac_[6] = {0};
    uint8_t broadcast_[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
};

class StubStorage : public StorageInterface {
public:
    size_t write(const std::string&, const std::string&, const std::string&) override { return 0; }
    std::string read(const std::string&, const std::string&, const std::string& def) override { return def; }
    bool remove(const std::string&, const std::string&) override { return true; }
    bool clear(const std::string&) override { return true; }
    void end() override {}
    uint8_t readUChar(const std::string&, const std::string&, uint8_t def) override { return def; }
    size_t writeUChar(const std::string&, const std::string&, uint8_t) override { return 0; }
    uint32_t readUInt(const std::string&, const std::string&, uint32_t def) override { return def; }
    size_t writeUInt(const std::string&, const std::string&, uint32_t) override { return 0; }
    size_t writeBytes(const std::string&, const std::string&, const uint8_t*, size_t) override {
        return 0;
    }
    size_t readBytes(const std::string&, const std::string&, uint8_t*, size_t) override {
        return 0;
    }
};

class StubHttpClient : public HttpClientInterface {
public:
    void setWifiConfig(WifiConfig*) override {}
    bool isConnected() override { return false; }
    bool queueRequest(HttpRequest&&) override { return false; }
    void disconnect() override {}
    void updateConfig(WifiConfig*) override {}
    void retryConnection() override {}
    uint8_t* getMacAddress() override { return mac_; }
    void setHttpClientState(HttpClientState) override {}
    HttpClientState getHttpClientState() override {
        return HttpClientState::DISCONNECTED;
    }
    bool hasQueuedRequests() override { return false; }

private:
    uint8_t mac_[6] = {0};
};

static MatchPacket makeDrawResultPacket(const std::optional<Match>& match, bool senderIsHunter) {
    MatchPacket p = {};
    strncpy(p.matchId, match->getMatchId(), sizeof(p.matchId) - 1);
    p.isHunter       = senderIsHunter;
    p.playerDrawTime = senderIsHunter ? match->getHunterDrawTime() : match->getBountyDrawTime();
    p.command = MatchCmd::DRAW_RESULT;
    return p;
}

static void deliver(StubPeerComms& target, const uint8_t mac[6], MatchPacket& pkt) {
    target.deliver(PktType::kMatchCommand, mac, reinterpret_cast<const uint8_t*>(&pkt),
                   sizeof(pkt));
}

// ============================================================
// Device context — one per simulated device
// ============================================================

struct DeviceCtx {
    StubPeerComms    peerComms;
    StubStorage      storage;
    StubHttpClient   httpClient;
    WirelessManager* wirelessMgr = nullptr;
    MatchManager*    matchMgr       = nullptr;
    Player           player;

    void init(const char* userId, bool isHunter) {
        char id[5];
        strncpy(id, userId, 4); id[4] = '\0';
        player.setUserID(id);
        player.setIsHunter(isHunter);

        wirelessMgr = new WirelessManager(&peerComms, &httpClient);
        matchMgr = new MatchManager(&player, &storage, wirelessMgr, nullptr);
    }

    void destroy() {
        delete matchMgr;
        delete wirelessMgr;
    }
};

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv) {
    long numMatches = 10000;
    if (argc >= 2) {
        numMatches = atol(argv[1]);
        if (numMatches < 1) numMatches = 1;
    }

    // Silence all LOG_* calls — prevents printf/stream overhead from
    // drowning out the business-logic signal in the profile.
    NullLogger nullLogger;
    g_logger = &nullLogger;

    StepClock clock;
    SimpleTimer::setPlatformClock(&clock);

    // Initialize the ID generator singleton with a fixed seed for reproducibility
    IdGenerator::initialize(42);

    DeviceCtx hunter, bounty;
    hunter.init("hunt", true);
    bounty.init("boun", false);

    const uint8_t kHunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    const uint8_t kBountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

    long hunterWins = 0, bountyWins = 0, errors = 0;

    fprintf(stderr, "Running %ld match cycles (no logging)...\n", numMatches);
    const auto wallStart = std::chrono::steady_clock::now();

    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(100, 1500);

    // --------------------------------------------------------
    // Hot loop — this is what the profiler should see
    // --------------------------------------------------------
    for (long i = 0; i < numMatches; ++i) {
        // Advance simulated time so each match starts from a fresh baseline
        clock.set(10000 + i * 5000UL);
        const unsigned long matchStart = clock.time_ms;

        // Hunter initiates the match via the production path.
        hunter.matchMgr->initializeMatch(const_cast<uint8_t*>(kBountyMac));

        // Bypass listenForMatchEvents(SEND_MATCH_ID), which requires a non-null RDC.
        const char* matchId = hunter.matchMgr->getCurrentMatch()->getMatchId();
        bounty.matchMgr->receiveMatch(matchId, true, const_cast<uint8_t*>(kHunterMac));

        MatchCommand ackCmd(kBountyMac, MatchCmd::MATCH_ID_ACK, matchId, 0, false);
        hunter.matchMgr->listenForMatchEvents(ackCmd);

        hunter.matchMgr->setMatchLocalStartTime(matchStart);
        bounty.matchMgr->setMatchLocalStartTime(matchStart);

        const long hunterPress = dist(gen);
        const long bountyPress = dist(gen);

        clock.set(matchStart + hunterPress);
        hunter.matchMgr->getMatchButtonPush()(hunter.matchMgr);

        clock.set(matchStart + bountyPress);
        bounty.matchMgr->getMatchButtonPush()(bounty.matchMgr);

        MatchPacket hunterPkt = makeDrawResultPacket(hunter.matchMgr->getCurrentMatch(), true);
        MatchPacket bountyPkt = makeDrawResultPacket(bounty.matchMgr->getCurrentMatch(), false);

        deliver(bounty.peerComms, kHunterMac, hunterPkt);
        deliver(hunter.peerComms, kBountyMac, bountyPkt);

        // Sanity check: both sides must agree on the winner
        const bool hw = hunter.matchMgr->didWin();
        const bool bw = bounty.matchMgr->didWin();
        if (hw == bw) {
            ++errors;
        } else {
            hw ? ++hunterWins : ++bountyWins;
        }

        // Reset for next iteration
        hunter.matchMgr->clearCurrentMatch();
        bounty.matchMgr->clearCurrentMatch();
    }
    // --------------------------------------------------------

    const auto wallEnd = std::chrono::steady_clock::now();
    const double wallMs =
        std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();

    fprintf(stderr, "\n=== Match Simulation Results ===\n");
    fprintf(stderr, "Matches run:  %ld\n", numMatches);
    fprintf(stderr, "Hunter wins:  %ld (%.1f%%)\n", hunterWins, 100.0 * hunterWins / numMatches);
    fprintf(stderr, "Bounty wins:  %ld (%.1f%%)\n", bountyWins, 100.0 * bountyWins / numMatches);
    fprintf(stderr, "Errors:       %ld\n",        errors);
    fprintf(stderr, "Wall time:    %.2f ms\n",    wallMs);
    fprintf(stderr, "Avg/match:    %.4f ms\n", wallMs / numMatches);
    fprintf(stderr, "Throughput:   %.0f matches/s\n", numMatches / (wallMs / 1000.0));

    hunter.destroy();
    bounty.destroy();
    SimpleTimer::setPlatformClock(nullptr);

    return errors > 0 ? 1 : 0;
}

#endif // NATIVE_BUILD && PERF_BUILD
