---
description: C++ constants and enum conventions for PDN firmware
globs: **/*.hpp, **/*.cpp
alwaysApply: false
---

# Constants

All constants use `SCREAMING_SNAKE_CASE` regardless of scope.

## File-scope constants
```cpp
// pdn-constants.hpp
constexpr uint8_t PRIMARY_BUTTON_PIN = 15;
constexpr const char* PREF_NAMESPACE = "matches";
```

## Class-scope constants
```cpp
class Duel : public ConnectState<PDN> {
    static constexpr int DUEL_TIMEOUT = 4000;
    static constexpr int HAPTIC_DURATION = 80;
};
```

## Log tags
`#define` in `.cpp` files only — never in headers.
```cpp
#define DUEL_TAG "DUEL_STATE"
```

---

# Enums

## Domain enums → `enum class`
Use when the **named value** is the concept. The fact that a type is serialized over a wire is incidental — cast explicitly at the serialization boundary and keep the type strongly typed everywhere else.
```cpp
enum class Phase { IDLE, GATHERING, ACTIVE };
enum class DeviceType { PDN, FDN, UNKNOWN };
```
```cpp
// Serializing — one explicit cast at the wire boundary
payload.deviceType = static_cast<uint8_t>(DeviceType::PDN);
// Deserializing — validate, then cast back
DeviceType type = static_cast<DeviceType>(rawByte);
```

## Integer-protocol registries → `namespace` + `constexpr int`
Use when the **integer value** is the concept — framework registry keys, state IDs passed directly as `int` to an API, wire command byte tables with no richer domain meaning.
```cpp
namespace QuickdrawStateId {
    constexpr int SLEEP  = 6;
    constexpr int IDLE   = 8;
    constexpr int DUEL   = 14;
}
// Usage: Sleep(QuickdrawStateId::SLEEP)
```
Plain unscoped `enum` is not used for new code.
