#pragma once

#include <memory>
#include "state-types.hpp"

class Device;

/*
 * StateLifecycle defines the internal dispatch contract used by StateMachine.
 * Concrete state classes should NOT inherit this directly — instead, inherit
 * State (device-agnostic) or TypedState<DeviceT> (device-specific).
 *
 * The mount/loop/dismount methods are the only entry points StateMachine calls.
 * TypedState<DeviceT> overrides them as final to perform a single cast from
 * Device* to DeviceT*, then forwards to the typed onState* user API.
 */
class StateLifecycle {
public:
    virtual ~StateLifecycle() = default;

    virtual void mount(Device* device) {}
    virtual void loop(Device* device) {}
    virtual void dismount(Device* device) {}
};
