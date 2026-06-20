---
description: C++ formatting conventions for PDN firmware
globs: **/*.hpp, **/*.cpp
alwaysApply: false
---

# Formatting

## Indentation & Braces
- 4 spaces. No tabs.
- K&R brace style — opening brace on the same line.
- Always space before parenthesis in control flow: `if (`, `for (`, `while (`.

```cpp
// ✅
if (condition) {
    doThing();
}

// ❌
if(condition){
```

## Pointers & References
`*` and `&` attach to the type, not the name.
```cpp
Player* player;          // ✅
const std::string& name; // ✅
Player *player;          // ❌
```

## Headers
- `#pragma once` — no `#ifndef` guards.
- In `src/`: headers live alongside their `.cpp` files — no separate `include/` directory.
- In `lib/core/`: the standard `include/` / `src/` split is maintained.

## Include Ordering
1. Arduino / platform (`<Arduino.h>`)
2. Third-party libraries (`<WiFi.h>`, `<ArduinoJson.h>`)
3. *(blank line)*
4. Project headers grouped by subsystem

In `.cpp` files: own `.hpp` first, then siblings, then stdlib.

## Constructor Initializer Lists
Colon on the same line as the signature; leading comma style for members.
```cpp
MatchManager::MatchManager(Player* player, StorageInterface* storage)
    : player(player)
    , storage(storage)
    , quickdrawWirelessManager(nullptr) {
}
```
Never assign members via `this->member = param` in the constructor body.

## Class Layout
Access modifier order: `public` → `protected` → `private`.
