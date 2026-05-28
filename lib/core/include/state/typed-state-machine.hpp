#pragma once

#include "state-machine.hpp"

/*
 * TypedStateMachine provides a typed cast helper for state machines that
 * operate on a specific Device subclass. The Device* parameter passed to
 * all lifecycle methods can be cast to the concrete subtype via castDevice().
 *
 * Usage:
 *   class Quickdraw : public TypedStateMachine<PDN> { ... };
 *
 * Device-agnostic state machines (e.g. HandshakeApp) should inherit directly
 * from StateMachine and never call castDevice().
 *
 * The static_cast is safe because each target's main.cpp exclusively wires
 * the correct concrete Device subclass to its own typed state machines.
 */
template<typename DeviceT>
class TypedStateMachine : public StateMachine {
public:
    using StateMachine::StateMachine;

protected:
    static DeviceT* castDevice(Device* device) {
        return static_cast<DeviceT*>(device);
    }
};
