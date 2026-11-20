#pragma once

#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "sim-devices.hpp"

namespace soak {

inline constexpr uint64_t TICK_MS = 5;
inline constexpr uint64_t HELLO_CADENCE_MS = 20;
// The event's wall clock at virtual time zero on the fake server (2026-11-18).
inline constexpr int64_t EVENT_EPOCH_MS = 1795000000000LL;
// A PDN's clock runs up to this fast or slow; the FDN's own clock is exact.
inline constexpr int32_t MAX_DRIFT_PPM = 100;

struct SoakConfig {
    uint64_t seed = 1;
    int pdns = 8;
    int fdns = 2;
    MediumConfig medium;
};

// One simulated event: PDNs and FDNs on one medium, cables between their jacks,
// and the server their uploads reach. PDN slot i is jack device i; FDN slot i is
// jack device -1 - i.
class World {
public:
    /** Builds the event for @p config; setUp() registers and boots the devices. */
    explicit World(const SoakConfig& config)
        : config(config)
        , medium(config.seed ^ 0x11, now)
        , cables([this](const JackRef& jack) { return resolveJack(jack); }) {
        medium.config = config.medium;
    }

    /** Not copyable: devices hold references into the world. */
    World(const World&) = delete;
    /** Not assignable. */
    World& operator=(const World&) = delete;

    /** Frees every device and restores the clock it found. */
    ~World() {
        destroyAll(pdns);
        destroyAll(fdns);
        delete server;
        SimpleTimer::setPlatformClock(savedClock);
    }

protected:
    template <class Device> struct Slot {
        Device* device = nullptr;
        SimNvs nvs;
        VirtualClock* clock = nullptr;
        uint8_t mac[6] = {};
    };
    struct PdnSlot : Slot<SimPdn> {
        PdnIdentity identity;
    };
    struct FdnSlot : Slot<SimFdn> {
        std::mt19937_64 rng;
    };

    template <class S> void destroyAll(std::vector<S>& slots) {
        for (S& slot : slots) {
            enter(slot);
            delete slot.device;
            delete slot.clock;
        }
    }

    // Each device runs with its own clock installed.
    template <class S> static void enter(S& slot) { SimpleTimer::setPlatformClock(slot.clock); }

    // Registers two teams and a player per PDN (hunters on even slots), then boots
    // every device.
    bool setUp() {
        const uint64_t seed = config.seed;
        server = new FakeServer(seed ^ 0x22);
        std::mt19937_64 rng(seed ^ 0x33);
        Team teams[2];
        strncpy(teams[0].name, "Hunter", sizeof(teams[0].name) - 1);
        strncpy(teams[1].name, "Bounty", sizeof(teams[1].name) - 1);
        if (!server->addTeam("Hunter", teams[0].id) || !server->addTeam("Bounty", teams[1].id))
            return false;
        pdns.resize(static_cast<size_t>(config.pdns));
        for (size_t i = 0; i < pdns.size(); i++) {
            PdnSlot& slot = pdns[i];
            const uint8_t mac[6] = {
            0x02, 0x50, 0, 0, static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
            memcpy(slot.mac, mac, 6);
            slot.identity.team = teams[i % 2];
            if (!server->addPlayer("pdn-" + std::to_string(i), slot.identity.team.id,
                                   slot.identity.playerId)) {
                return false;
            }
            const int32_t drift =
            static_cast<int32_t>(rng() % (2 * MAX_DRIFT_PPM + 1)) - MAX_DRIFT_PPM;
            slot.clock = new VirtualClock(now, drift);
            bootPdn(static_cast<int>(i));
        }
        fdns.resize(static_cast<size_t>(config.fdns));
        for (size_t i = 0; i < fdns.size(); i++) {
            FdnSlot& slot = fdns[i];
            const uint8_t mac[6] = {0x02, 0xFD, 0, 0, 0, static_cast<uint8_t>(i)};
            memcpy(slot.mac, mac, 6);
            slot.rng.seed(seed ^ (0x44 + i));
            slot.clock = new VirtualClock(now, 0);
            bootFdn(static_cast<int>(i));
        }
        return true;
    }

    void bootPdn(int index) {
        PdnSlot& slot = pdns[static_cast<size_t>(index)];
        slot.clock->boot(false);
        enter(slot);
        slot.device =
        new SimPdn("_" + std::to_string(index), medium, slot.mac, slot.nvs, slot.identity);
    }

    void bootFdn(int index) {
        FdnSlot& slot = fdns[static_cast<size_t>(index)];
        slot.clock->boot(true);
        // The FDN's wall clock is taken as right: it reads the server's Date header.
        slot.clock->setUnixMilliseconds(EVENT_EPOCH_MS + static_cast<int64_t>(now));
        enter(slot);
        slot.device =
        new SimFdn("_f" + std::to_string(index), medium, slot.mac, slot.nvs, *server, slot.rng);
    }

    NativeSerialDriver* resolveJack(const JackRef& jack) {
        if (jack.device >= 0) {
            PdnSlot& slot = pdns[static_cast<size_t>(jack.device)];
            return slot.device != nullptr ? slot.device->jack(jack.jack) : nullptr;
        }
        FdnSlot& slot = fdns[static_cast<size_t>(-jack.device - 1)];
        return slot.device != nullptr ? slot.device->jack(jack.jack) : nullptr;
    }

    // One step of virtual time: the air, the cables, then every device in slot order.
    void tick() {
        now += TICK_MS;
        medium.deliverDue();
        cables.transfer(allJacks());
        const bool hello = now % HELLO_CADENCE_MS == 0;
        for (PdnSlot& slot : pdns)
            step(slot, hello);
        for (FdnSlot& slot : fdns)
            step(slot, hello);
    }

    template <class S> static void step(S& slot, bool hello) {
        enter(slot);
        if (hello) slot.device->firmware->getRemoteDeviceCoordinator()->emitHello();
        slot.device->firmware->loop();
    }

    std::vector<JackRef> allJacks() const {
        std::vector<JackRef> out;
        for (size_t i = 0; i < pdns.size(); i++) {
            out.push_back({static_cast<int>(i), SerialIdentifier::OUTPUT_JACK});
            out.push_back({static_cast<int>(i), SerialIdentifier::INPUT_JACK});
        }
        for (size_t i = 0; i < fdns.size(); i++) {
            out.push_back({-static_cast<int>(i) - 1, SerialIdentifier::INPUT_JACK});
            out.push_back({-static_cast<int>(i) - 1, SerialIdentifier::INPUT_JACK_SECONDARY});
        }
        return out;
    }

    SoakConfig config;
    uint64_t now = 0;
    VirtualMedium medium;
    VirtualCables cables;
    FakeServer* server = nullptr;
    std::vector<PdnSlot> pdns;
    std::vector<FdnSlot> fdns;
    PlatformClock* savedClock = SimpleTimer::getPlatformClock();
};

}  // namespace soak
