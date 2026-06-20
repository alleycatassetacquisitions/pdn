---
description: C++ naming conventions for PDN firmware
globs: **/*.hpp, **/*.cpp
alwaysApply: false
---

# Naming Conventions

## General
- camelCase for all names unless a more specific rule applies below.
- Names should be descriptive full words — no abbreviations unless universally understood (`pdn`, `mac`, `rdc`).

## Classes & Structs
PascalCase noun phrases that describe the thing's role.
```cpp
class MatchManager { };
struct ActiveDuelState { };
```

## Methods — prefix signals intent

| Prefix | Use |
|---|---|
| `on` | Event handlers and lifecycle hooks (`onStateMounted`, `onRoleAnnouncePacket`) |
| `get` | Non-boolean accessors (`getWins()`, `getLightManager()`) |
| `is` / `has` / `did` | Boolean queries (`isMatchReady()`, `hasReceivedResult()`, `didWin()`) |
| `transitionTo` | State predicate methods — always paired with a `transitionToXState` flag |
| `set` | Mutating injectors (`setBoostProvider()`, `setRemoteDeviceCoordinator()`) |
| (verb phrase) | Actions (`initializeMatch()`, `clearCurrentMatch()`, `renderStats()`) |

## Variables

- **Manager/coordinator pointers:** exactly the camelCase class name — `matchManager`, `remoteDeviceCoordinator`
- **Timers:** noun + `Timer` — `dormantTimer`, `matchInitializationTimer`
- **Transition flags:** `transitionToXState` matching the predicate `transitionToX()`
- **Boolean members:** descriptive state phrase — `displayIsDirty`, `matchInitialized`

## Files
kebab-case with `.hpp` / `.cpp`. State files: `{state-name}-state.cpp`.
