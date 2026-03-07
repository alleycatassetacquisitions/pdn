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
#include "wireless/quickdraw-wireless-manager.hpp"

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
    void setPacketHandler(PktType, PacketCallback, void*) override {}
    void clearPacketHandler(PktType) override {}
    const uint8_t* getGlobalBroadcastAddress() override { return broadcast_; }
    uint8_t* getMacAddress() override { return mac_; }
    void removePeer(uint8_t*) override {}
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
    void setWifiConfig(WifiConfig*) override {}
    bool isConnected() override { return false; }
    bool queueRequest(HttpRequest&) override { return false; }
    void disconnect() override {}
    void updateConfig(WifiConfig*) override {}
    void retryConnection() override {}
    uint8_t* getMacAddress() override { return mac_; }
    void setHttpClientState(HttpClientState) override {}
    HttpClientState getHttpClientState() override {
        return HttpClientState::DISCONNECTED;
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

static void deliver(QuickdrawWirelessManager* target,
                    const uint8_t mac[6],
                    QuickdrawPacket& pkt) {
    target->processQuickdrawCommand(
        mac,
        reinterpret_cast<const uint8_t*>(&pkt),
        sizeof(pkt));
}

// ============================================================
// Device context — one per simulated device
// ============================================================

struct DeviceCtx {
    StubPeerComms    peerComms;
    StubStorage      storage;
    StubHttpClient   httpClient;
    WirelessManager* wirelessMgr   = nullptr;
    QuickdrawWirelessManager* qdWireless = nullptr;
    MatchManager*    matchMgr       = nullptr;
    Player           player;

    void init(const char* userId, bool isHunter, const char* opponentMac) {
        char id[5];
        strncpy(id, userId, 4); id[4] = '\0';
        player.setUserID(id);
        player.setIsHunter(isHunter);
        player.setOpponentMacAddress(opponentMac);

        wirelessMgr = new WirelessManager(&peerComms, &httpClient);
        qdWireless  = new QuickdrawWirelessManager();
        matchMgr    = new MatchManager();

        qdWireless->initialize(&player, wirelessMgr, /*broadcastCooldown=*/0);
        matchMgr->initialize(&player, &storage, qdWireless);
    }

    void armCallback() {
        qdWireless->setPacketReceivedCallback(
            std::bind(&MatchManager::listenForMatchEvents,
                      matchMgr, std::placeholders::_1));
    }

    void destroy() {
        delete matchMgr;
        delete qdWireless;
        delete wirelessMgr;
    }
};

// ============================================================
// Main
// ============================================================

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
    hunter.init("hunt", true,  "BB:BB:BB:BB:BB:BB");
    bounty.init("boun", false, "AA:AA:AA:AA:AA:AA");

    const uint8_t kHunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    const uint8_t kBountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

    // Wire each device's wireless manager to its own MatchManager callback
    hunter.armCallback();
    bounty.armCallback();

    long hunterWins = 0, bountyWins = 0, errors = 0;

    fprintf(stderr, "Running %ld duel cycles (no logging)...\n", numDuels);
    const auto wallStart = std::chrono::steady_clock::now();

    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(100, 1500);

    // --------------------------------------------------------
    // Hot loop — this is what the profiler should see
    // --------------------------------------------------------
    for (long i = 0; i < numDuels; ++i) {
        // Advance simulated time so each duel starts from a fresh baseline
        clock.set(10000 + i * 5000UL);
        const unsigned long duelStart = clock.time_ms;

        // Hunter initiates the match via the production path.
        hunter.matchMgr->initializeMatch(const_cast<uint8_t*>(kBountyMac));

        // Deliver SEND_MATCH_ID to bounty via listenForMatchEvents directly
        // (we're benchmarking match logic, not serialization overhead).
        const char* matchId = hunter.matchMgr->getCurrentMatch()->getMatchId();
        QuickdrawCommand sendMatchCmd(kBountyMac, QDCommand::SEND_MATCH_ID,
                                      matchId, "hunt", 0, true);
        bounty.matchMgr->listenForMatchEvents(sendMatchCmd);

        // Deliver MATCH_ID_ACK back to hunter
        QuickdrawCommand ackCmd(kHunterMac, QDCommand::MATCH_ID_ACK,
                                matchId, "boun", 0, false);
        hunter.matchMgr->listenForMatchEvents(ackCmd);

        hunter.matchMgr->setDuelLocalStartTime(duelStart);
        bounty.matchMgr->setDuelLocalStartTime(duelStart);

        const long hunterPress = dist(gen);
        const long bountyPress = dist(gen);

        clock.set(duelStart + hunterPress);
        hunter.matchMgr->getDuelButtonPush()(hunter.matchMgr);

        clock.set(duelStart + bountyPress);
        bounty.matchMgr->getDuelButtonPush()(bounty.matchMgr);

        // Exchange draw results via wire-format packets through processQuickdrawCommand.
        QuickdrawPacket hunterPkt = makeDrawResultPacket(
            hunter.matchMgr->getCurrentMatch(), true, "hunt");
        QuickdrawPacket bountyPkt = makeDrawResultPacket(
            bounty.matchMgr->getCurrentMatch(), false, "boun");

        deliver(bounty.qdWireless, kHunterMac, hunterPkt);
        deliver(hunter.qdWireless, kBountyMac, bountyPkt);

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
