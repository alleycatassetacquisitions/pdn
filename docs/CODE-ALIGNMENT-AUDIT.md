# Code Alignment Audit Report
## FinallyEve/pdn vs alleycatassetacquisitions/pdn

**Date**: 2026-02-15
**Auditor**: Agent 02 (Senior Engineer)
**Reference**: Upstream PR #81 (app-as-state), PR #90 (sub-apps), design primer

---

## Executive Summary

**Overall Alignment Score: 7/10**

Our fork maintains strong alignment with upstream patterns in core areas (state lifecycle, transitions, hardware abstraction), but has diverged in file organization and lacks full adoption of the sub-app predicate API pattern formalized in upstream PR #90.

**Key Strengths:**
- ✅ State lifecycle pattern fully adopted (onStateMounted/Loop/Dismounted)
- ✅ Transition predicate pattern with std::bind wiring
- ✅ No Device pointer storage violations
- ✅ Proper use of SimpleTimer (no blocking calls)
- ✅ Header guard compliance (pragma once)
- ✅ Naming conventions mostly followed

**Key Gaps:**
- ⚠️ File structure divergence: `include/game/*` vs upstream `include/apps/*`
- ⚠️ Sub-app predicate API incomplete (KonamiMetaGame)
- ⚠️ State ID allocation not fully documented/rationalized
- ⚠️ Two files still use `#ifndef` header guards
- ⚠️ One file naming violation (UUID.cpp)

---

## 1. State Lifecycle Compliance ✅

**Status**: EXCELLENT

All state classes audited implement the three mandatory lifecycle methods:
- `void onStateMounted(Device *PDN)`
- `void onStateLoop(Device *PDN)`
- `void onStateDismounted(Device *PDN)`

**Evidence**:
- `include/game/quickdraw-states.hpp`: All states declare lifecycle methods
- `include/game/fdn-states.hpp`: NPC states follow pattern
- `include/game/*/[game]-states.hpp`: All minigame states compliant

**Critical Pass**: No states found storing `Device *PDN` as member variable (grep search returned no matches).

**Sample Review** (PlayerRegistration):
```cpp
class PlayerRegistration : public State {
public:
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    // Device is passed as parameter, never stored ✅
};
```

**Cleanup Pattern**: States properly invalidate callbacks and reset hardware in `onStateDismounted()` (observed in FetchUserDataState, ConfirmOfflineState).

---

## 2. Transition Pattern Compliance ✅

**Status**: EXCELLENT

All state machines use the predicate + std::bind pattern for transitions.

**Pattern Evidence** (`src/game/quickdraw.cpp:76-99`):
```cpp
playerRegistration->addTransition(
    new StateTransition(
        std::bind(&PlayerRegistration::transitionToUserFetch, playerRegistration),
        fetchUserData));

fetchUserData->addTransition(
    new StateTransition(
        std::bind(&FetchUserDataState::transitionToConfirmOffline, fetchUserData),
        confirmOffline));
```

**Predicate Implementation** (typical):
```cpp
// In state header
bool transitionToUserFetch();

// Private flag
bool transitionToUserFetchState = false;

// In .cpp
bool PlayerRegistration::transitionToUserFetch() {
    return transitionToUserFetchState;
}
```

**No violations found**: No direct state pointer manipulation detected.

---

## 3. File Structure Alignment ⚠️

**Status**: MODERATE DIVERGENCE

### Current Structure
```
include/
├── apps/
│   ├── handshake.hpp          ✅ Follows upstream pattern
│   └── player-registration.hpp ✅ Follows upstream pattern
├── game/
│   ├── quickdraw.hpp          ⚠️ Should be in apps/quickdraw/
│   ├── quickdraw-states.hpp   ⚠️ Should be in apps/quickdraw/
│   ├── konami-metagame.hpp    ⚠️ Should be in apps/konami-metagame/
│   ├── fdn-game.hpp           ⚠️ Generic wrapper, should be in apps/
│   ├── signal-echo/           ⚠️ Minigames should be in apps/
│   ├── ghost-runner/          ⚠️ Each minigame = app
│   ├── spike-vector/
│   ├── cipher-path/
│   ├── exploit-sequencer/
│   ├── breach-defense/
│   └── firewall-decrypt/
```

### Upstream Pattern (PR #90)
```
include/
└── apps/
    ├── <app-name>/
    │   ├── <app-name>.hpp           [StateMachine subclass]
    │   └── <app-name>-states.hpp    [State enums + classes]
```

### Impact
- **Technical**: None (code functions correctly)
- **Maintainability**: Future upstream merges will have path conflicts
- **Clarity**: "game" directory conflates main app (Quickdraw) with minigames (Signal Echo, etc.)

### Recommendation
**Priority**: Moderate (fix when refactoring, not urgent)

**Migration Path**:
1. Create `include/apps/quickdraw/` and move quickdraw.hpp, quickdraw-states.hpp
2. Create `include/apps/konami-metagame/` and move konami-metagame files
3. Move each minigame to `include/apps/<minigame-name>/`
4. Shared resources (player.hpp, match.hpp, progress-manager.hpp) stay in `include/game/`
5. Update all `#include` paths
6. Update platformio.ini build_flags if needed

---

## 4. Naming Convention Compliance ✅ (mostly)

**Status**: GOOD

### Classes (PascalCase) ✅
- `PlayerRegistration`, `FetchUserDataState`, `HandshakeApp` all compliant
- No snake_case class names found

### Enums (SHOUTY_SNAKE_CASE) ✅
All state ID enums follow pattern:
```cpp
enum QuickdrawStateId {
    PLAYER_REGISTRATION = 0,
    FETCH_USER_DATA = 1,
    // ...
};

enum SignalEchoStateId {
    ECHO_INTRO = 100,
    ECHO_SHOW_SEQUENCE = 101,
    // ...
};
```

### Files (kebab-case) ⚠️
**Violation**: `src/utils/UUID.cpp` should be `uuid.cpp`

All other files follow kebab-case:
- `quickdraw-states.hpp` ✅
- `ghost-runner-gameplay.cpp` ✅
- `konami-code-entry.hpp` ✅

### Header Guards (pragma once) ⚠️
**Violations**:
- `include/images-raw.hpp` uses `#ifndef IMAGES_RAW_H`
- `include/game/quickdraw-resources.hpp` uses `#ifndef QUICKDRAW_RESOURCES_H`

All other 50+ headers use `#pragma once` ✅

### Recommendation
**Priority**: Low (cosmetic, but easy fix)

1. Rename `src/utils/UUID.cpp` → `uuid.cpp`
2. Replace `#ifndef` guards with `#pragma once` in 2 files
3. Update any broken includes (unlikely, but verify with build)

---

## 5. Anti-Pattern Detection ✅

**Status**: EXCELLENT

### No Blocking Calls ✅
- Searched for `delay()`, `sleep()`, blocking `while()` loops
- All timing uses `SimpleTimer` with state machine integration
- Example: `FetchUserDataState::userDataFetchTimer` (non-blocking timeout)

### No Global Mutable State ✅
- Player, MatchManager, ProgressManager all passed as dependencies
- No singleton pattern abuse
- Snapshot pattern not yet needed (no pause/resume requirement)

### No Direct Internal State Access ✅
- HandshakeApp exposes predicates: `readyForCountdown()`, `timedOut()` ✅
- MiniGame base class exposes `isGameComplete()`, `getOutcome()` ✅

---

## 6. API Surface Review ⚠️

**Status**: PARTIALLY ALIGNED

### HandshakeApp (Sub-App Pattern) ✅
**Follows PR #90 pattern perfectly:**
```cpp
// include/apps/handshake.hpp
class HandshakeApp : public StateMachine {
public:
    bool readyForCountdown();  // ✅ Predicate for parent
    bool timedOut();           // ✅ Predicate for parent
};
```

**Usage in parent** (presumed, not shown in audit):
```cpp
// Quickdraw checks HandshakeApp via predicates
if (handshakeApp->readyForCountdown()) {
    transitionToDuelCountdownState = true;
}
```

### KonamiMetaGame (Sub-App Pattern) ⚠️
**Missing predicate API:**
```cpp
// include/game/konami-metagame/konami-metagame.hpp
class KonamiMetaGame : public StateMachine {
    // ❌ No public predicates for Quickdraw to check
    // Parent can't query "is Konami code complete?" or "which boon awarded?"
};
```

**Expected Pattern** (based on upstream):
```cpp
class KonamiMetaGame : public StateMachine {
public:
    bool isCodeEntryComplete();      // Predicate for parent
    bool shouldReturnToIdle();       // Predicate for parent
    KonamiButton getAwardedButton(); // State query
};
```

### MiniGame Base Class ✅
**Good predicate design:**
```cpp
class MiniGame : public StateMachine {
public:
    bool isGameComplete() const { return outcome.isComplete(); }
    const MiniGameOutcome& getOutcome() const { return outcome; }
    // Parent uses these to decide when to return from minigame
};
```

**Usage** (`fdn-detected-state.cpp:125-130`):
```cpp
auto* game = static_cast<MiniGame*>(PDN->getApp(StateId(appId)));
game->resetGame();
PDN->setActiveApp(StateId(appId));
// Later in FdnComplete, parent checks game->isGameComplete()
```

### FdnGame (Generic Wrapper) ⚠️
**Unclear role:**
```cpp
// include/game/fdn-game.hpp
constexpr int FDN_GAME_APP_ID = 10;

class FdnGame : public StateMachine {
    // Has game type, reward, result tracking
    // But not clear if this is used vs MiniGame base class
};
```

**Question**: Is `FdnGame` redundant with `MiniGame`? Audit suggests minigames inherit from `MiniGame`, not `FdnGame`. `FdnGame` may be legacy or planned feature.

### Recommendation
**Priority**: High for KonamiMetaGame (needed for clean parent-child API)

1. **KonamiMetaGame**: Add predicates for Quickdraw to query completion state
2. **FdnGame**: Document purpose or deprecate if redundant with MiniGame
3. **All future sub-apps**: Follow HandshakeApp pattern (expose predicates, hide internal state)

---

## 7. State ID Allocation Analysis

**Status**: FUNCTIONAL BUT UNDOCUMENTED

### Current Allocation
| App | App ID | State ID Range | Notes |
|-----|--------|---------------|-------|
| Player Registration | 0 | N/A (embedded in Quickdraw) | Legacy? |
| Quickdraw | 1 | 0-32 | Main game |
| Handshake | 3 | 9-12 | Embedded in Quickdraw state range |
| Signal Echo | 4 | 100-107 | Well-spaced |
| Ghost Runner | 5 | 300-305 | Large gap from Signal Echo |
| Spike Vector | 6 | 400-405 | |
| Cipher Path | 7 | 500-506 | |
| Exploit Sequencer | 8 | 600-606 | |
| Breach Defense | 11 | 700-706 | App ID 11 but IDs in 700s |
| Konami Meta | 9 | 0-14 (?) | Uses KONAMI_HANDSHAKE = 0 (collision?) |
| FDN Game | 10 | 0-3 | Uses NPC_IDLE = 0 (collision?) |
| Firewall Decrypt | ? | 200-204 | No app ID found |

### Issues
1. **Collision risk**: Multiple apps use state ID 0 (Quickdraw, KonamiMeta, FdnGame)
   - *Mitigated by*: Apps run in isolation, states scoped to parent StateMachine
   - *But*: Confusing for debugging, violates upstream "contiguous per app" rule

2. **Inconsistent spacing**: Signal Echo uses 100s, Ghost Runner jumps to 300s (why not 200s?)

3. **Missing app IDs**: Firewall Decrypt has state IDs but no `constexpr int FIREWALL_DECRYPT_APP_ID`

### Upstream Pattern
- **App IDs**: Allocated contiguously (0, 1, 2, 3...)
- **State IDs**: Contiguous within app, starting from 0 or offset (30+ for FDN per primer)

### Recommendation
**Priority**: Low (functional issue, but good hygiene)

1. Document state ID allocation strategy in `device/device-types.hpp` or new `state-id-registry.hpp`
2. Ensure all apps have `constexpr int <APP>_APP_ID` defined
3. Rationalize state ID ranges (e.g., all minigames use 1000-1999, offset by game ID)
4. Prevent actual collisions (audit suggests isolated execution prevents runtime issues)

---

## 8. Critical Issues (Must Fix)

### None Found ✅

All critical patterns are compliant:
- State lifecycle implementation
- Transition mechanism
- Hardware abstraction via Device parameter
- No blocking calls
- No global mutable state

---

## 9. Moderate Issues (Fix When Touching Files)

### 1. File Structure Divergence
**Impact**: Future merge conflicts with upstream
**Effort**: Medium (path updates across 50+ files)
**Trigger**: Next time we refactor Quickdraw or add new minigame

### 2. KonamiMetaGame Missing Predicates
**Impact**: Parent can't cleanly query completion state
**Effort**: Low (add 3-5 predicate methods)
**Trigger**: Next time we extend Konami progression system

### 3. Header Guard Violations (2 files)
**Impact**: Style inconsistency only
**Effort**: Trivial (2-line change per file)
**Trigger**: Next time we edit those files

---

## 10. Minor Issues (Nice to Have)

### 1. File Naming (UUID.cpp)
**Impact**: Style inconsistency
**Effort**: Trivial (rename + update includes)

### 2. State ID Allocation Documentation
**Impact**: Developer confusion when adding new apps
**Effort**: Low (write doc, maybe consolidate IDs)

### 3. FdnGame Role Clarification
**Impact**: Potential code debt if unused
**Effort**: Low (document or deprecate)

---

## 11. Recommendations by Module

### Quickdraw (Main App)
- **Move to** `include/apps/quickdraw/` when doing next major refactor
- **Add** top-level app predicates if Quickdraw becomes callable as sub-app
- **Status**: Core patterns excellent, file location moderate issue

### HandshakeApp
- **Status**: EXEMPLARY — perfect sub-app pattern implementation ✅
- **Action**: Use as reference for future sub-apps

### KonamiMetaGame
- **Add predicates**: `isCodeEntryComplete()`, `shouldReturnToIdle()`, `getAwardedButton()`
- **Move to** `include/apps/konami-metagame/` when file structure refactor happens
- **Priority**: High (needed for clean API)

### Minigames (Signal Echo, Ghost Runner, etc.)
- **Status**: Good adherence to MiniGame base class pattern ✅
- **Move to** `include/apps/<minigame>/` during file structure refactor
- **Ensure** all have `constexpr int <GAME>_APP_ID` defined
- **Priority**: Low (functional, just organizational)

### Player, ProgressManager, MatchManager
- **Status**: Shared utilities, correctly not in app structure ✅
- **Keep in** `include/game/` as shared resources

---

## 12. Upstream Sync Recommendations

### What to Pull from Upstream
1. **AppConfig pattern refinements** (if PR #90 merged to upstream main)
2. **State pause/resume** (Snapshot pattern) if we add game save/load
3. **Any new driver interfaces** (check upstream device/drivers/)

### What We Should Push Upstream
1. **MiniGame base class** — clean abstraction for sub-games
2. **DifficultyScaler** — progression-aware difficulty
3. **CLI simulator enhancements** (keyboard, display mirror, commands)
4. **Test coverage patterns** (split .hpp/.cpp registration)

### Divergence We Should Keep
1. **Multiple minigames** — our unique gameplay content
2. **Progression system** — NVS-backed player state, Konami meta-progression
3. **Test infrastructure** — 262 native + 180 CLI tests (upstream has basic tests)

---

## 13. Conclusion

Our fork maintains excellent alignment with upstream core patterns (state lifecycle, transitions, hardware abstraction) and has extended the architecture in compatible ways (MiniGame base class, sub-app pattern in HandshakeApp).

The main divergence is **file organization** — we use `include/game/*` where upstream is moving to `include/apps/*`. This is a **moderate-priority refactor** that should be done proactively to ease future merges.

The only **code-level gap** is KonamiMetaGame's missing predicate API, which should be added when extending the progression system.

**Overall assessment**: We can confidently continue development on our current architecture. No breaking changes needed, just organizational cleanup when convenient.

---

## Appendix: Audit Commands

```bash
# File structure
find include/ -type f -name "*.hpp" | grep -E "(game|apps|state)" | sort

# Device pointer violations (none found)
grep -r "Device \*PDN;" include/

# Blocking calls (none found)
grep -r "\bdelay\(|sleep\(" src/

# Header guards
grep -r "^#ifndef" include/

# State lifecycle
grep -r "class.*: public State" include/ | wc -l  # 50+ states checked

# Transition pattern
grep -A5 "addTransition" src/game/quickdraw.cpp | head -20
```

---

**End of Audit Report**
