---
description: Modern C++ usage rules for PDN firmware
globs: **/*.hpp, **/*.cpp
alwaysApply: false
---

# Modern C++ Usage

## nullptr
Always `nullptr`. Never `NULL` or `0` for pointers.

## auto
Strongly discouraged. Only use when the type is already written explicitly on the same line.
```cpp
auto* proposal = new ShootoutProposal(sht, cdm); // ✅ type visible on RHS
auto result = matchManager->getCurrentMatch();    // ❌ type not obvious
```
Good naming conventions make `auto` unnecessary in all other contexts.

## Lambdas
Lambdas are the single pattern for all callbacks and predicates. `std::bind` is not used.

The capture list documents what the closure depends on:
```cpp
[x]()          // depends on one object
[x, y]()       // depends on multiple objects — common for multi-condition predicates
[this]()       // belongs to the owning class
```

## Casts
`static_cast<T*>` always. C-style casts `(T*)` are not used in new code.

## Const correctness
- All member functions that do not mutate state must be marked `const`.
- Prefer `const T&` parameters for objects passed by reference.

## Explicit constructors
All single-argument constructors must be marked `explicit`.
```cpp
explicit Sleep(Player* player);   // ✅
Sleep(Player* player);            // ❌
```

## Override
`override` is required on every virtual method override. Never omit it.

## Memory
Raw `new` / `delete` — no smart pointers. Initialize all pointers to `nullptr`.
In destructors: null the pointer, then `delete` owned sub-objects. Never just null without deleting.
