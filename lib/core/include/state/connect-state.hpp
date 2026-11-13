#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"

#include <array>
#include <cstring>
#include <functional>
#include <optional>

/**
 * The peer's self-description on one jack, copied out of coordinator storage so
 * it stays valid past the next context on that jack and past link death. Opaque
 * here: `peerType` says which struct `profile` holds, and the game layer decodes
 * it.
 */
struct ConnectionContext {
    DeviceType peerType = DeviceType::UNKNOWN;
    ChainRole chainRole = ChainRole::STANDALONE;
    std::array<uint8_t, RemoteDeviceCoordinator::MAX_PEER_PROFILE_BYTES> profile{};
    size_t profileLen = 0;
};

/**
 * A jack's connection plus, on a connect, the peer's context. Absent on a
 * disconnect, and absent on a connect the exchange has not stored a profile for
 * — which the link SM makes unreachable today, its one transition into CONNECTED
 * being gated on that exchange completing.
 */
struct JackConnectionState {
    bool connected = false;
    std::optional<ConnectionContext> context;
};

/*
 * Knows which of its device's jacks are cabled from the coordinator's
 * jack-change edges. Subscribed while mounted, keyed on `this` so its dismount
 * and its destructor remove only its own registration.
 */
template <typename DeviceT>
class ConnectState : public TypedState<DeviceT> {
public:
    /** Binds the state to the coordinator whose jack statuses it watches. */
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : TypedState<DeviceT>(stateId)
        , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

    /** Removes its jack-change registration if still subscribed; the coordinator is not owned. */
    ~ConnectState() override {
        // App teardown deletes states without dismounting them, which would leave a
        // `this`-bound lambda in the device-owned coordinator.
        if (subscribed) unsubscribe();
        remoteDeviceCoordinator = nullptr;
    }

    using JackStateHandler =
        std::function<void(SerialIdentifier jack, const JackConnectionState& state)>;

    /**
     * Registers the handler for every jack edge during this state's tenure: a
     * connect or disconnect on a jack this device owns, plus a replay of each
     * already-connected jack at mount.
     *
     * Register once, from the constructor or from onStateMounted — the mount replay
     * runs after both, and the slot is not cleared on dismount, so one registration
     * covers every tenure. A handler capturing anything shorter-lived than the state
     * must replace itself before that thing dies.
     *
     * Handlers must tolerate re-receiving an event they already acted on, and only
     * *connected* jacks are replayed — a disconnect between tenures is never
     * delivered, so reset per-jack state in onStateDismounted rather than waiting
     * for a clear. isConnected() already reflects the edge when the handler runs.
     * On a disconnect the coordinator has not caught up yet: its getJackStatus
     * still reads CONNECTED for that jack.
     *
     * The context is read at dispatch time rather than captured when it arrived,
     * so a state mounted long after the peer connected still gets it.
     */
    void setOnJackState(JackStateHandler handler) {
        jackStateHandler = std::move(handler);
    }

    /**
     * The hardware kind of the peer connected on `jack`, read when its connect
     * edge was delivered. UNKNOWN when nothing is connected there.
     */
    DeviceType getPeerDeviceType(SerialIdentifier jack) const {
        return jacks[jackSlot(jack)].peerType;
    }

    /**
     * True when ANY required jack is connected (either, not both), per the jack
     * edges delivered this tenure. False outside a tenure and inside the
     * subclass's own onStateMounted and onStateDismounted.
     */
    bool isConnected() {
        return (isPrimaryRequired() && isJackConnected(SerialIdentifier::OUTPUT_JACK)) ||
               (isAuxRequired() && isJackConnected(SerialIdentifier::INPUT_JACK)) ||
               (isSecondaryRequired() && isJackConnected(SerialIdentifier::INPUT_JACK_SECONDARY));
    }

    /**
     * No required jack has been connected for the full debounce window. The
     * window opens on the disconnect edge, or at mount for a state mounted with
     * nothing cabled, and closes on the next connect.
     */
    bool isPersistentlyDisconnected() { return disconnectWindow.expired(); }

    /**
     * Restarts the disconnect window, so a loss already under way has to last a
     * fresh full window from now.
     */
    void resetDisconnectDebounce() {
        disconnectWindow.invalidate();
        syncDisconnectWindow();
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    virtual bool isPrimaryRequired() = 0;
    virtual bool isAuxRequired() = 0;
    // Override and return true to also consider INPUT_JACK_SECONDARY when
    // evaluating isConnected(). Devices with a single input jack leave this false.
    virtual bool isSecondaryRequired() { return false; }

private:
    // What one jack's edges have said during this tenure.
    struct JackRecord {
        bool connected = false;
        DeviceType peerType = DeviceType::UNKNOWN;
    };

    // TypedState brackets the subclass hooks with these, so the replay lands after
    // the subclass's own mount hook rather than reaching members it has not set up.
    void afterMount(DeviceT* device) final {
        subscribe();
        replayConnectedJacks();
        // A state mounted with nothing cabled gets no edge to open its window on.
        syncDisconnectWindow();
    }

    // Edges between tenures are not delivered, so what this tenure learned is
    // dropped with it; the next mount's replay rebuilds it.
    void beforeDismount(DeviceT* device) final {
        if (subscribed) unsubscribe();
        jacks.fill(JackRecord{});
        disconnectWindow.invalidate();
    }

    void subscribe() {
        remoteDeviceCoordinator->setOnJackChange(
        [this](SerialIdentifier jack, bool connected) { dispatchJackState(jack, connected); },
        this);
        subscribed = true;
    }

    void dispatchJackState(SerialIdentifier jack, bool connected) {
        JackRecord& record = jacks[jackSlot(jack)];
        record.connected = connected;
        // On a disconnect the coordinator still names the peer that just left.
        record.peerType =
        connected ? remoteDeviceCoordinator->getPeerDeviceType(jack) : DeviceType::UNKNOWN;
        syncDisconnectWindow();
        if (jackStateHandler) jackStateHandler(jack, buildJackState(jack, connected));
    }

    // Opens the window when the last required jack goes and closes it when one
    // comes back. A window already open keeps its start.
    void syncDisconnectWindow() {
        if (isConnected()) {
            disconnectWindow.invalidate();
            return;
        }
        if (!disconnectWindow.isRunning()) disconnectWindow.setTimer(DISCONNECT_DEBOUNCE_MS);
    }

    // On a disconnect the link is mid-teardown, so the peer facts still describe
    // whoever just left; handing them over as a live context would invite acting
    // on a departed peer.
    JackConnectionState buildJackState(SerialIdentifier jack, bool connected) const {
        JackConnectionState state;
        state.connected = connected;
        if (!connected) return state;

        size_t length = 0;
        const uint8_t* profile = remoteDeviceCoordinator->getPeerProfile(jack, length);
        if (profile == nullptr) return state;

        ConnectionContext context;
        context.peerType = remoteDeviceCoordinator->getPeerDeviceType(jack);
        context.chainRole = remoteDeviceCoordinator->getPeerChainRole(jack);
        context.profileLen = length;
        memcpy(context.profile.data(), profile, length);
        state.context = context;
        return state;
    }

    void unsubscribe() {
        remoteDeviceCoordinator->setOnJackChange(nullptr, this);
        subscribed = false;
    }

    void replayConnectedJacks() {
        // The jack status is the link machine's own state, and the link machine
        // is the only source of jack edges, so every connect replayed here has a
        // matching disconnect still to come.
        for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
            if (remoteDeviceCoordinator->getJackStatus(jack) == JackStatus::CONNECTED) {
                dispatchJackState(jack, true);
            }
        }
    }

    static size_t jackSlot(SerialIdentifier jack) {
        switch (jack) {
            case SerialIdentifier::OUTPUT_JACK: return 0;
            case SerialIdentifier::INPUT_JACK: return 1;
            case SerialIdentifier::INPUT_JACK_SECONDARY: return 2;
        }
        return 0;
    }

    bool isJackConnected(SerialIdentifier jack) const { return jacks[jackSlot(jack)].connected; }

    static constexpr unsigned long DISCONNECT_DEBOUNCE_MS = 500;
    static constexpr size_t JACK_COUNT = 3;
    SimpleTimer disconnectWindow;
    JackStateHandler jackStateHandler;
    std::array<JackRecord, JACK_COUNT> jacks{};
    bool subscribed = false;
};
