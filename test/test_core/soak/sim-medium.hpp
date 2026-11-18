#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <vector>

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/native/native-serial-driver.hpp"

namespace soak {

/** A MAC as one integer, for map keys. */
inline uint64_t macKey(const uint8_t* mac) {
    uint64_t key = 0;
    for (int i = 0; i < 6; i++)
        key = (key << 8) | mac[i];
    return key;
}

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

    /** Sets the wall clock, as an FDN context or an HTTP Date header does. */
    void setUnixMilliseconds(int64_t unixMs) override {
        syncedUnixMs = unixMs;
        syncedAt = worldNow;
        synced = true;
    }

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
};

class SimRadio;

// The shared ESP-NOW air: per-frame loss, delay, duplication, reorder and loss
// bursts, broadcast and unicast by MAC. A unicast sender learns the outcome the
// way the ESP32 does, from a send status: success only when the frame arrived
// and its link-layer ack was not lost.
class VirtualMedium {
public:
    using AirObserver =
    std::function<void(const uint8_t* src, PktType type, const std::vector<uint8_t>& data)>;

    /** Seeds the medium's own stream, so its faults replay with the run. */
    VirtualMedium(uint64_t seed, const uint64_t& worldNow)
        : rng(seed)
        , worldNow(worldNow) {}

    /** Drops frames still in the air. */
    ~VirtualMedium() {
        while (!inFlight.empty()) {
            delete inFlight.top();
            inFlight.pop();
        }
    }

    /** Called by a radio on initialize(); returns its instance number. */
    uint64_t attach(SimRadio* radio, const uint8_t* mac) {
        radios[macKey(mac)] = radio;
        return ++instances;
    }

    /** Called by a radio's destructor; frames to it are dropped from now on. */
    void detach(const uint8_t* mac, SimRadio* radio) {
        std::map<uint64_t, SimRadio*>::iterator it = radios.find(macKey(mac));
        if (it != radios.end() && it->second == radio) radios.erase(it);
    }

    /** Queues one frame from @p from; see SimRadio::sendData. */
    void send(SimRadio& from, const uint8_t* dst, PktType type, const uint8_t* data, size_t length);

    /** Delivers every frame and send status due by now. */
    void deliverDue();

    /** True when @p mac neither hears nor reaches anyone (power cut, FDN out of range). */
    bool isMuted(const uint8_t* mac) const { return muted.count(macKey(mac)) != 0; }

    /** Mutes or unmutes @p mac. */
    void setMuted(const uint8_t* mac, bool mute) {
        if (mute) {
            muted[macKey(mac)] = true;
        } else {
            muted.erase(macKey(mac));
        }
    }

    MediumConfig config;
    // Loss probability while a burst runs, and the virtual time it ends.
    double burstLoss = 0;
    uint64_t burstUntil = 0;
    AirObserver onAir;
    uint64_t framesSent = 0;
    uint64_t framesLost = 0;

private:
    struct Frame {
        uint64_t at;
        uint64_t order;
        uint8_t src[6];
        uint8_t dst[6];
        PktType type;
        std::vector<uint8_t> data;
        bool lost;
        bool ackLost;
        bool wantsStatus;
        uint64_t senderInstance;
    };
    struct Later {
        bool operator()(const Frame* a, const Frame* b) const {
            return a->at != b->at ? a->at > b->at : a->order > b->order;
        }
    };

    bool chance(double p) { return p > 0 && std::uniform_real_distribution<double>(0, 1)(rng) < p; }
    void schedule(const SimRadio& from, const uint8_t* dst, PktType type, const uint8_t* data,
                  size_t length, bool wantsStatus);

    std::mt19937_64 rng;
    const uint64_t& worldNow;
    std::map<uint64_t, SimRadio*> radios;
    std::map<uint64_t, bool> muted;
    std::priority_queue<Frame*, std::vector<Frame*>, Later> inFlight;
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

    /** Fires queued receives, then queued send statuses. */
    void exec() override {
        std::queue<Event> pending;
        std::swap(pending, events);
        while (!pending.empty()) {
            Event& event = pending.front();
            if (event.isStatus) {
                --awaitingStatus;
                std::map<PktType, StatusHandler>::iterator it = statusHandlers.find(event.type);
                if (it != statusHandlers.end()) {
                    it->second.callback(event.mac, event.data.data(), event.data.size(),
                                        event.success, it->second.context);
                }
            } else if (state == PeerCommsState::ENABLED) {
                std::map<PktType, PacketHandler>::iterator it = handlers.find(event.type);
                if (it != handlers.end()) {
                    it->second.callback(event.mac, event.data.data(), event.data.size(),
                                        it->second.context);
                }
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

    /** True when no unicast frame awaits its send status. */
    bool isSendQueueEmpty() override { return awaitingStatus == 0; }

    /** FF:FF:FF:FF:FF:FF. */
    const uint8_t* getGlobalBroadcastAddress() override { return BROADCAST; }

    /** The fixed address. */
    uint8_t* getMacAddress() override { return macAddress; }

    /** Enables or disables, as WirelessManager switches modes. */
    void setPeerCommsState(PeerCommsState newState) override { state = newState; }

    /** ENABLED or DISABLED. */
    PeerCommsState getPeerCommsState() override { return state; }

    /** Enables the radio. */
    void enable() override { state = PeerCommsState::ENABLED; }

    /** Disables the radio. */
    void disable() override { state = PeerCommsState::DISABLED; }

    /** Medium side: a frame arrives; kept only while enabled. */
    bool receive(const uint8_t* src, PktType type, const std::vector<uint8_t>& data) {
        if (state != PeerCommsState::ENABLED) return false;
        events.push({false, type, {}, data, false});
        memcpy(events.back().mac, src, 6);
        return true;
    }

    /** Medium side: the verdict on one of this radio's unicast frames. */
    void reportStatus(const uint8_t* dst, PktType type, const std::vector<uint8_t>& data,
                      bool success) {
        events.push({true, type, {}, data, success});
        memcpy(events.back().mac, dst, 6);
    }

    /** Medium side: a unicast frame now awaits its status. */
    void noteAwaitingStatus() { ++awaitingStatus; }

    /** Medium side: which attach this radio is, so a reboot's statuses don't cross. */
    uint64_t getInstance() const { return instance; }

    static constexpr uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

private:
    struct PacketHandler {
        PacketCallback callback;
        void* context;
    };
    struct StatusHandler {
        SendStatusCallback callback;
        void* context;
    };
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
    std::map<PktType, PacketHandler> handlers;
    std::map<PktType, StatusHandler> statusHandlers;
    std::queue<Event> events;
    size_t awaitingStatus = 0;
};

inline void VirtualMedium::send(SimRadio& from, const uint8_t* dst, PktType type,
                                const uint8_t* data, size_t length) {
    if (onAir) onAir(from.getMacAddress(), type, std::vector<uint8_t>(data, data + length));
    if (memcmp(dst, SimRadio::BROADCAST, 6) != 0) {
        from.noteAwaitingStatus();
        schedule(from, dst, type, data, length, true);
        if (chance(config.duplicate)) schedule(from, dst, type, data, length, false);
        return;
    }
    // Every listener draws its own fate; the sender's verdict is always success.
    for (const std::pair<const uint64_t, SimRadio*>& entry : radios) {
        if (entry.second != &from) {
            schedule(from, entry.second->getMacAddress(), type, data, length, false);
        }
    }
}

inline void VirtualMedium::schedule(const SimRadio& from, const uint8_t* dst, PktType type,
                                    const uint8_t* data, size_t length, bool wantsStatus) {
    Frame* frame = new Frame();
    uint32_t delay =
    std::uniform_int_distribution<uint32_t>(config.minDelayMs, config.maxDelayMs)(rng);
    if (chance(config.reorder)) delay += std::uniform_int_distribution<uint32_t>(10, 40)(rng);
    frame->at = worldNow + delay;
    frame->order = ++order;
    memcpy(frame->src, const_cast<SimRadio&>(from).getMacAddress(), 6);
    memcpy(frame->dst, dst, 6);
    frame->type = type;
    frame->data.assign(data, data + length);
    const double loss = worldNow < burstUntil ? burstLoss : config.loss;
    frame->lost = chance(loss) || isMuted(frame->src);
    frame->ackLost = chance(config.ackLoss);
    frame->wantsStatus = wantsStatus;
    frame->senderInstance = from.getInstance();
    ++framesSent;
    inFlight.push(frame);
}

inline void VirtualMedium::deliverDue() {
    while (!inFlight.empty() && inFlight.top()->at <= worldNow) {
        Frame* frame = inFlight.top();
        inFlight.pop();
        std::map<uint64_t, SimRadio*>::iterator dst = radios.find(macKey(frame->dst));
        bool delivered = false;
        if (!frame->lost && dst != radios.end() && !isMuted(frame->dst)) {
            delivered = dst->second->receive(frame->src, frame->type, frame->data);
        }
        if (!delivered) ++framesLost;
        std::map<uint64_t, SimRadio*>::iterator src = radios.find(macKey(frame->src));
        if (frame->wantsStatus && src != radios.end() &&
            src->second->getInstance() == frame->senderInstance) {
            src->second->reportStatus(frame->dst, frame->type, frame->data,
                                      delivered && !frame->ackLost);
        }
        delete frame;
    }
}

/** One jack of one device, by device slot. */
struct JackRef {
    int device = -1;
    SerialIdentifier jack = SerialIdentifier::OUTPUT_JACK;
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
        for (size_t i = 0; i < cables.size(); i++) {
            if (same(cables[i].a, end) || same(cables[i].b, end)) {
                cables.erase(cables.begin() + static_cast<long>(i));
                return;
            }
        }
    }

    /** True when @p end holds a cable. */
    bool isPlugged(const JackRef& end) const {
        for (const Cable& cable : cables) {
            if (same(cable.a, end) || same(cable.b, end)) return true;
        }
        return false;
    }

    /** Moves each cable's bytes both ways; unplugged jacks' output falls on the floor. */
    void transfer(const std::vector<JackRef>& allJacks) {
        for (const Cable& cable : cables) {
            move(cable.a, cable.b);
            move(cable.b, cable.a);
        }
        for (const JackRef& jack : allJacks) {
            NativeSerialDriver* driver = isPlugged(jack) ? nullptr : resolve(jack);
            if (driver != nullptr) driver->clearOutput();
        }
    }

private:
    struct Cable {
        JackRef a;
        JackRef b;
    };

    static bool same(const JackRef& x, const JackRef& y) {
        return x.device == y.device && x.jack == y.jack;
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
    std::vector<Cable> cables;
};

}  // namespace soak
