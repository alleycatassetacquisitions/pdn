#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "wireless/mac-functions.hpp"

namespace soak {

// One device's clock over the world's virtual time. millis restarts at each boot;
// the wall clock survives a soft reboot (the ESP32 RTC) and is lost with power.
class VirtualClock : public PlatformClock {
public:
    // ROM and bootloader time before the app's first millis() read on an ESP32-S3.
    static constexpr uint64_t BOOT_MS = 350;

    /** Reads the world's virtual time through @p worldNow; @p driftPpm skews this device. */
    VirtualClock(const uint64_t& worldNow, int32_t driftPpm)
        : worldNow(worldNow)
        , driftPpm(driftPpm) {}

    /** Milliseconds since this device last booted. */
    unsigned long milliseconds() override {
        return static_cast<unsigned long>(skewed(worldNow - bootAt + BOOT_MS));
    }

    /** UTC once synced; until then the uptime since power-on, as an unset ESP32 reads. */
    int64_t unixMilliseconds() override {
        if (!synced) return skewed(worldNow - powerOnAt + BOOT_MS);
        return syncedUnixMs + skewed(worldNow - syncedAt);
    }

    /** Sets the wall clock. */
    void setUnixMilliseconds(int64_t unixMs) override {
        syncedUnixMs = unixMs;
        syncedAt = worldNow;
        synced = true;
    }

    /** Virtual milliseconds since the wall clock was last set. */
    uint64_t millisecondsSinceSync() const { return worldNow - syncedAt; }

    /** Restarts millis; @p powerLost also drops the wall clock. */
    void boot(bool powerLost) {
        bootAt = worldNow;
        if (!powerLost) return;
        powerOnAt = worldNow;
        synced = false;
    }

    bool synced = false;

private:
    int64_t skewed(uint64_t elapsed) const {
        return static_cast<int64_t>(elapsed) + static_cast<int64_t>(elapsed) * driftPpm / 1000000;
    }

    const uint64_t& worldNow;
    int32_t driftPpm = 0;
    uint64_t bootAt = 0;
    uint64_t powerOnAt = 0;
    uint64_t syncedAt = 0;
    int64_t syncedUnixMs = 0;
};

struct MediumConfig {
    double loss = 0.02;
    double duplicate = 0.005;
    double ackLoss = 0.01;
    double reorder = 0.02;
    uint32_t minDelayMs = 1;
    uint32_t maxDelayMs = 6;
    // A reordered frame waits 10 ms up to this much longer.
    uint32_t maxReorderDelayMs = 40;
};

/** True with probability @p p; a zero @p p draws nothing, so an unused fault keeps @p rng. */
inline bool chance(std::mt19937_64& rng, double p) {
    return p > 0 && std::uniform_real_distribution<double>(0, 1)(rng) < p;
}

class SimRadio;

// The shared ESP-NOW air: per-frame loss, delay, duplication, reorder and loss
// bursts, broadcast and unicast by MAC. A unicast sender learns the outcome the
// way the ESP32 does, from a send status: success only when the frame arrived
// and its link-layer ack was not lost.
class VirtualMedium {
public:
    /** Seeds the medium's own stream, so its faults replay with the run. */
    VirtualMedium(uint64_t seed, const uint64_t& worldNow)
        : rng(seed)
        , worldNow(worldNow) {}

    /** Delivers every frame and send status due by now. */
    void deliverDue();

    /** Mutes or unmutes @p mac: it neither hears nor reaches anyone. */
    void setMuted(const uint8_t* mac, bool mute) {
        if (mute) {
            muted.insert(MacToUInt64(mac));
        } else {
            muted.erase(MacToUInt64(mac));
        }
    }

    MediumConfig config;
    // Loss probability while a burst runs, and the virtual time it ends.
    double burstLoss = 0;
    uint64_t burstUntil = 0;
    // Until then every record ack is lost: the FDN stored, the PDN never hears so.
    uint64_t dropAcksUntil = 0;
    uint64_t framesSent = 0;
    uint64_t framesLost = 0;

private:
    friend class SimRadio;
    struct Frame {
        uint64_t at;
        uint64_t order;
        uint8_t src[6], dst[6];
        PktType type;
        std::vector<uint8_t> data;
        bool lost, ackLost, wantsStatus;
        uint64_t senderInstance;
    };
    struct Later {
        /** Earliest delivery first; ties in send order. */
        bool operator()(const Frame& a, const Frame& b) const {
            return a.at != b.at ? a.at > b.at : a.order > b.order;
        }
    };

    bool isMuted(const uint8_t* mac) const { return muted.count(MacToUInt64(mac)) != 0; }

    // A radio joins on initialize() and gets its instance number; it leaves in its
    // destructor, and frames to it are dropped from then on.
    uint64_t attach(SimRadio* radio, const uint8_t* mac) {
        radios[MacToUInt64(mac)] = radio;
        return ++instances;
    }
    void detach(const uint8_t* mac, SimRadio* radio) {
        std::map<uint64_t, SimRadio*>::iterator it = radios.find(MacToUInt64(mac));
        if (it != radios.end() && it->second == radio) radios.erase(it);
    }
    void send(SimRadio& from, const uint8_t* dst, PktType type, const uint8_t* data, size_t length);
    void schedule(const SimRadio& from, const uint8_t* dst, PktType type, const uint8_t* data,
                  size_t length, bool wantsStatus);

    std::mt19937_64 rng;
    const uint64_t& worldNow;
    std::map<uint64_t, SimRadio*> radios;
    std::set<uint64_t> muted;
    std::priority_queue<Frame, std::vector<Frame>, Later> inFlight;
    uint64_t instances = 0;
    uint64_t order = 0;
};

// ESP-NOW driver over the VirtualMedium with a fixed MAC, so a rebooted device
// keeps its address. Receive and send status are queued and fire in exec(), as
// the ESP32 driver's callbacks reach the main loop.
class SimRadio : public PeerCommsDriverInterface {
public:
    /** A radio named @p name with address @p mac on @p medium. */
    SimRadio(const std::string& name, VirtualMedium& medium, const uint8_t* mac)
        : PeerCommsDriverInterface(name)
        , medium(medium) {
        memcpy(macAddress, mac, sizeof(macAddress));
    }

    /** Leaves the medium. */
    ~SimRadio() override { medium.detach(macAddress, this); }

    /** Joins the medium. */
    int initialize() override {
        instance = medium.attach(this, macAddress);
        return 0;
    }

    /** Fires queued receives and send statuses in the order the medium posted them. */
    void exec() override {
        std::queue<Event> pending;
        std::swap(pending, events);
        while (!pending.empty()) {
            Event& event = pending.front();
            if (event.isStatus) {
                std::pair<SendStatusCallback, void*>& handler = statusHandlers[event.type];
                if (handler.first) {
                    handler.first(event.mac, event.data.data(), event.data.size(), event.success,
                                  handler.second);
                }
            } else if (state == PeerCommsState::ENABLED) {
                std::pair<PacketCallback, void*>& handler = handlers[event.type];
                if (handler.first)
                    handler.first(event.mac, event.data.data(), event.data.size(), handler.second);
            }
            pending.pop();
        }
    }

    /** Refused while disabled, like the ESP32 driver; otherwise onto the air. */
    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data,
                 const size_t length) override {
        if (state != PeerCommsState::ENABLED) return -1;
        medium.send(*this, dst, packetType, data, length);
        return 0;
    }

    /** One handler per packet type. */
    void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) override {
        handlers[packetType] = {std::move(callback), ctx};
    }
    /** Drops the packet type's handler. */
    void clearPacketHandler(PktType packetType) override { handlers.erase(packetType); }
    /** One send-status handler per packet type. */
    void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) override {
        statusHandlers[packetType] = {std::move(callback), ctx};
    }
    /** Drops the packet type's send-status handler. */
    void clearSendStatusHandler(PktType packetType) override { statusHandlers.erase(packetType); }
    /** FF:FF:FF:FF:FF:FF. */
    const uint8_t* getGlobalBroadcastAddress() override { return BROADCAST; }
    /** The fixed address. */
    uint8_t* getMacAddress() override { return macAddress; }
    /** Enables or disables. */
    void setPeerCommsState(PeerCommsState newState) override { state = newState; }
    /** ENABLED or DISABLED. */
    PeerCommsState getPeerCommsState() override { return state; }
    /** Enables the radio. */
    void enable() override { state = PeerCommsState::ENABLED; }
    /** Disables the radio. */
    void disable() override { state = PeerCommsState::DISABLED; }

    static constexpr uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

private:
    friend class VirtualMedium;
    struct Event {
        bool isStatus;
        PktType type;
        uint8_t mac[6];
        std::vector<uint8_t> data;
        bool success;
    };

    VirtualMedium& medium;
    uint8_t macAddress[6] = {};
    uint64_t instance = 0;
    PeerCommsState state = PeerCommsState::DISABLED;
    std::map<PktType, std::pair<PacketCallback, void*>> handlers;
    std::map<PktType, std::pair<SendStatusCallback, void*>> statusHandlers;
    std::queue<Event> events;

    // Medium side: a received frame (status false) or the verdict on one of this
    // radio's unicast frames, handled on the next exec().
    void post(bool isStatus, const uint8_t* mac, PktType type, const std::vector<uint8_t>& data,
              bool success) {
        events.push({isStatus, type, {}, data, success});
        memcpy(events.back().mac, mac, 6);
    }
};

inline void VirtualMedium::send(SimRadio& from, const uint8_t* dst, PktType type,
                                const uint8_t* data, size_t length) {
    if (memcmp(dst, SimRadio::BROADCAST, 6) != 0) {
        schedule(from, dst, type, data, length, true);
        if (chance(rng, config.duplicate)) schedule(from, dst, type, data, length, false);
        return;
    }
    // Every listener draws its own fate; a broadcast gets no send status.
    for (const std::pair<const uint64_t, SimRadio*>& entry : radios) {
        if (entry.second != &from) {
            schedule(from, entry.second->getMacAddress(), type, data, length, false);
        }
    }
}

inline void VirtualMedium::schedule(const SimRadio& from, const uint8_t* dst, PktType type,
                                    const uint8_t* data, size_t length, bool wantsStatus) {
    Frame frame;
    uint32_t delay =
    std::uniform_int_distribution<uint32_t>(config.minDelayMs, config.maxDelayMs)(rng);
    if (chance(rng, config.reorder))
        delay += std::uniform_int_distribution<uint32_t>(10, config.maxReorderDelayMs)(rng);
    frame.at = worldNow + delay;
    frame.order = ++order;
    memcpy(frame.src, from.macAddress, 6);
    memcpy(frame.dst, dst, 6);
    frame.type = type;
    frame.data.assign(data, data + length);
    const double loss = worldNow < burstUntil ? burstLoss : config.loss;
    frame.lost = chance(rng, loss) || isMuted(frame.src) ||
                 (type == PktType::kRecordAck && worldNow < dropAcksUntil);
    frame.ackLost = chance(rng, config.ackLoss);
    frame.wantsStatus = wantsStatus;
    frame.senderInstance = from.instance;
    ++framesSent;
    inFlight.push(std::move(frame));
}

inline void VirtualMedium::deliverDue() {
    while (!inFlight.empty() && inFlight.top().at <= worldNow) {
        const Frame frame = inFlight.top();
        inFlight.pop();
        std::map<uint64_t, SimRadio*>::iterator dst = radios.find(MacToUInt64(frame.dst));
        bool delivered = false;
        if (!frame.lost && dst != radios.end() && !isMuted(frame.dst) &&
            dst->second->state == PeerCommsState::ENABLED) {
            dst->second->post(false, frame.src, frame.type, frame.data, false);
            delivered = true;
        }
        if (!delivered) ++framesLost;
        std::map<uint64_t, SimRadio*>::iterator src = radios.find(MacToUInt64(frame.src));
        if (frame.wantsStatus && src != radios.end() &&
            src->second->instance == frame.senderInstance) {
            src->second->post(true, frame.dst, frame.type, frame.data, delivered && !frame.ackLost);
        }
    }
}

/** One jack of one device, by device slot. */
struct JackRef {
    int device = -1;
    SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK;

    /** Same device, same jack. */
    bool operator==(const JackRef& other) const {
        return device == other.device && jack == other.jack;
    }
};

// Serial cables between jacks. Bytes written on one end arrive at the other on
// the next transfer, through the real native serial driver and the RDC's HELLO
// parser. A jack holds at most one cable.
class VirtualCables {
public:
    using Resolver = std::function<NativeSerialDriver*(const JackRef&)>;

    /** @p resolve maps a jack to its live driver, or null while the device is down. */
    explicit VirtualCables(Resolver resolve)
        : resolve(std::move(resolve)) {}

    /** Plugs a cable; false when either jack already holds one. */
    bool plug(const JackRef& a, const JackRef& b) {
        if (isPlugged(a) || isPlugged(b)) return false;
        cables.push_back({a, b});
        return true;
    }

    /** Unplugs whatever cable sits in @p end. */
    void unplug(const JackRef& end) {
        cables.erase(std::remove_if(cables.begin(), cables.end(), holds(end)), cables.end());
    }

    /** True when @p end holds a cable. */
    bool isPlugged(const JackRef& end) const {
        return std::any_of(cables.begin(), cables.end(), holds(end));
    }

    /** Moves each cable's bytes both ways; unplugged jacks' output falls on the floor. */
    void transfer(const std::vector<JackRef>& allJacks) {
        for (const std::pair<JackRef, JackRef>& cable : cables) {
            move(cable.first, cable.second);
            move(cable.second, cable.first);
        }
        for (const JackRef& jack : allJacks) {
            NativeSerialDriver* driver = isPlugged(jack) ? nullptr : resolve(jack);
            if (driver != nullptr) driver->clearOutput();
        }
    }

private:
    static std::function<bool(const std::pair<JackRef, JackRef>&)> holds(const JackRef& end) {
        return [end](const std::pair<JackRef, JackRef>& cable) {
            return cable.first == end || cable.second == end;
        };
    }

    void move(const JackRef& from, const JackRef& to) {
        NativeSerialDriver* source = resolve(from);
        if (source == nullptr) return;
        const std::string bytes = source->getOutput();
        source->clearOutput();
        NativeSerialDriver* sink = resolve(to);
        if (sink == nullptr || bytes.empty()) return;
        sink->injectBytes(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    }

    Resolver resolve;
    std::vector<std::pair<JackRef, JackRef>> cables;
};

}  // namespace soak
