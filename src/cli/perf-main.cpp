#if defined(NATIVE_BUILD) && defined(PERF_BUILD)

/**
 * Duel Performance Harness
 *
 * Runs a large volume of complete hunter-vs-bounty duel cycles using only the
 * business logic layer. No GoogleTest, no GMock, no rendering, no I/O in the hot
 * path. Stubs satisfy every interface with no-op or minimal implementations.
 *
 * Build:  pio run -e native_perf
 * Run:    .pio/build/native_perf/program [NUM_DUELS]
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
#include "device/remote-device-coordinator.hpp"
#include "wireless/wireless-transport.hpp"

// Null logger: suppresses all LOG_* output in the hot path.
class NullLogger : public LoggerInterface {
public:
    void vlog(LogLevel, const char*, const char*, int,
              const char*, va_list) override {}
};

// Controllable fake clock: no syscall overhead.
class StepClock : public PlatformClock {
public:
    unsigned long milliseconds() override { return time_ms; }
    void set(unsigned long t) { time_ms = t; }
    void advance(unsigned long delta) { time_ms += delta; }
    unsigned long time_ms = 0;
};

class StubPeerComms : public PeerCommsInterface {
public:
    int sendData(const uint8_t*, PktType,
                 const uint8_t*, const size_t) override { return 1; }
    void setPacketHandler(PktType, PacketCallback, void*) override {}
    void clearPacketHandler(PktType) override {}
    const uint8_t* getGlobalBroadcastAddress() override { return broadcast_; }
    uint8_t* getMacAddress() override { return mac_; }
    int addEspNowPeer(const uint8_t*) override { return 0; }
    int removeEspNowPeer(const uint8_t*) override { return 0; }
    void setPeerCommsState(PeerCommsState) override {}
    PeerCommsState getPeerCommsState() override {
        return PeerCommsState::CONNECTED;
    }
    void connect() override {}
    void disconnect() override {}
private:
    uint8_t mac_[6] = {0};
    uint8_t broadcast_[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
};

// The hunter and bounty are each other's cable-established direct peer by
// construction here, so vouch for every sender; MatchManager gates
// SEND_MATCH_ID on this.
class StubRDC : public RemoteDeviceCoordinator {
public:
    bool isDirectPeer(const uint8_t*) const override { return true; }
};

class StubStorage : public StorageInterface {
public:
    size_t write(const std::string&, const std::string&) override { return 0; }
    std::string read(const std::string&,
                     const std::string& def) override { return def; }
    bool remove(const std::string&) override { return true; }
    bool clear() override { return true; }
    void end() override {}
    uint8_t readUChar(const std::string&, uint8_t def) override { return def; }
    size_t writeUChar(const std::string&, uint8_t) override { return 0; }
};

class StubHttpClient : public HttpClientInterface {
public:
    bool isConnected() override { return false; }
    bool queueRequest(HttpRequest&) override { return false; }
    void disconnect() override {}
    uint8_t* getMacAddress() override { return mac_; }
    void setHttpClientState(HttpClientState) override {}
    HttpClientState getHttpClientState() override {
        return HttpClientState::IDLE;
    }
private:
    uint8_t mac_[6] = {0};
};

// Build a QuickdrawPacket (the real wire format) carrying a player's draw result.
static QuickdrawPacket makeDrawResultPacket(const std::optional<Match>& match,
                                            bool senderIsHunter,
                                            const char* senderId) {
    QuickdrawPacket p = {};
    strncpy(p.matchId,  match->getMatchId(), sizeof(p.matchId) - 1);
    strncpy(p.playerId, senderId,            sizeof(p.playerId) - 1);
    p.isHunter       = senderIsHunter;
    p.playerDrawTime = senderIsHunter ? match->getHunterDrawTime()
                                      : match->getBountyDrawTime();
    p.command        = QDCommand::DRAW_RESULT;
    return p;
}

static void deliver(WirelessTransport* target,
                    const uint8_t mac[6],
                    QuickdrawPacket& pkt) {
    target->deliverIncoming(
        PktType::kQuickdrawCommand, 0, mac,
        reinterpret_cast<const uint8_t*>(&pkt),
        sizeof(pkt));
}

struct DeviceCtx {
    StubPeerComms    peerComms;
    StubStorage      storage;
    StubHttpClient   httpClient;
    StubRDC          rdc;
    WirelessManager* wirelessMgr   = nullptr;
    WirelessTransport* transport   = nullptr;
    MatchManager*    matchMgr       = nullptr;
    Player           player;

    void init(const char* userId, bool isHunter) {
        char id[5];
        strncpy(id, userId, 4); id[4] = '\0';
        player.setUserID(id);
        player.setIsHunter(isHunter);

        wirelessMgr = new WirelessManager(&peerComms, &httpClient);
        transport   = new WirelessTransport(wirelessMgr);
        matchMgr    = new MatchManager();

        matchMgr->initialize(&player, &storage, transport);
        matchMgr->setRemoteDeviceCoordinator(&rdc);
    }

    void destroy() {
        delete matchMgr;
        delete transport;
        delete wirelessMgr;
    }
};

int main(int argc, char** argv) {
    long numDuels = 10000;
    if (argc >= 2) {
        numDuels = atol(argv[1]);
        if (numDuels < 1) numDuels = 1;
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

    fprintf(stderr, "Running %ld duel cycles (no logging)...\n", numDuels);
    const auto wallStart = std::chrono::steady_clock::now();

    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(100, 1500);

    // Hot loop: this is what the profiler should see.
    for (long i = 0; i < numDuels; ++i) {
        // Advance simulated time so each duel starts from a fresh baseline
        clock.set(10000 + i * 5000UL);
        const unsigned long duelStart = clock.time_ms;

        // Hunter initiates the match via the production path.
        hunter.matchMgr->initializeMatch(const_cast<uint8_t*>(kBountyMac));

        // Deliver SEND_MATCH_ID to bounty via listenForMatchEvents directly
        // (we're benchmarking match logic, not serialization overhead).
        const char* matchId = hunter.matchMgr->getCurrentMatch()->getMatchId();
        bounty.matchMgr->listenForMatchEvents(
            kHunterMac, makeQuickdrawPacket(QDCommand::SEND_MATCH_ID,
                                            matchId, "hunt", 0, true));

        // Deliver MATCH_ID_ACK back to hunter
        hunter.matchMgr->listenForMatchEvents(
            kBountyMac, makeQuickdrawPacket(QDCommand::MATCH_ID_ACK,
                                            matchId, "boun", 0, false));

        hunter.matchMgr->setDuelLocalStartTime(duelStart);
        bounty.matchMgr->setDuelLocalStartTime(duelStart);

        const long hunterPress = dist(gen);
        const long bountyPress = dist(gen);

        clock.set(duelStart + hunterPress);
        hunter.matchMgr->getDuelButtonPush()(hunter.matchMgr);

        clock.set(duelStart + bountyPress);
        bounty.matchMgr->getDuelButtonPush()(bounty.matchMgr);

        // Exchange draw results via wire-format packets through the transport's
        // reliable channel (deliverIncoming -> ack/dedup -> onReceive).
        QuickdrawPacket hunterPkt = makeDrawResultPacket(
            hunter.matchMgr->getCurrentMatch(), true, "hunt");
        QuickdrawPacket bountyPkt = makeDrawResultPacket(
            bounty.matchMgr->getCurrentMatch(), false, "boun");

        deliver(bounty.transport, kHunterMac, hunterPkt);
        deliver(hunter.transport, kBountyMac, bountyPkt);

        // Sanity check: both sides must agree on the winner
        const bool hw = hunter.matchMgr->didWin();
        const bool bw = bounty.matchMgr->didWin();
        if (hw == bw) {
            ++errors;
        } else {
            hw ? ++hunterWins : ++bountyWins;
        }

        hunter.matchMgr->clearCurrentMatch();
        bounty.matchMgr->clearCurrentMatch();
    }

    const auto wallEnd = std::chrono::steady_clock::now();
    const double wallMs =
        std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();

    fprintf(stderr, "\n=== Duel Simulation Results ===\n");
    fprintf(stderr, "Duels run:    %ld\n",       numDuels);
    fprintf(stderr, "Hunter wins:  %ld (%.1f%%)\n", hunterWins,
            100.0 * hunterWins / numDuels);
    fprintf(stderr, "Bounty wins:  %ld (%.1f%%)\n", bountyWins,
            100.0 * bountyWins / numDuels);
    fprintf(stderr, "Errors:       %ld\n",        errors);
    fprintf(stderr, "Wall time:    %.2f ms\n",    wallMs);
    fprintf(stderr, "Avg/duel:     %.4f ms\n",    wallMs / numDuels);
    fprintf(stderr, "Throughput:   %.0f duels/s\n",
            numDuels / (wallMs / 1000.0));

    hunter.destroy();
    bounty.destroy();
    SimpleTimer::setPlatformClock(nullptr);

    return errors > 0 ? 1 : 0;
}

#endif // NATIVE_BUILD && PERF_BUILD
