#pragma once

#include "state-machine.hpp"

/*
 * TypedStateMachine<DeviceT> is a convenience base for state machines whose
 * own lifecycle methods (onStateMounted, onStateLoop, onStateDismounted) need
 * a typed device pointer.
 *
 * Individual states inside the machine should inherit TypedState<DeviceT> from
 * state.hpp — they receive the correctly typed pointer directly and do not need
 * castDevice() at all.
 *
 * castDevice() here is only for code inside the state machine class itself, for
 * example when Quickdraw::onStateMounted needs to call PDN-specific setup.
 *
 * Device-agnostic state machines should inherit
 * StateMachine directly.
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
