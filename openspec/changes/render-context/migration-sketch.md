# Migration Sketch: idle-state and duel-state

This document shows what `idle-state.cpp` and `duel-state.cpp` look like after migrating to the `applyRenderState` / `RenderEngine` pattern. It is a reference for the migration tasks, not a normative spec.

Key things to observe:
- `onStateMounted` calls `applyRenderState` with a fully populated `InterfaceFrame` — describes what the device should look like on entry
- `onStateLoop` calls `applyRenderState` with only the modules that changed — null modules mean "leave as-is"
- `onStateDismounted` is pure business logic — no driver calls, no `removeButtonCallbacks()`
- `renderStats()` becomes a helper that calls `applyRenderState` with a display-only delta
- `displayIsDirty` is gone — the state just calls `applyRenderState` when it wants an update
- `RenderEngine::clearFrame()` on every transition handles all teardown automatically

---

## idle-state.cpp — before

```cpp
void Idle::onStateMounted(PDN* pdn) {
    pdn->getWirelessManager()->enablePeerCommsMode();

    AnimationBase* animation;
    AnimationConfig config;
    if (player->isHunter()) {
        animation = new IdleAnimation();
        config.speed = 16;
        config.curve = EaseCurve::LINEAR;
        config.initialState = HUNTER_IDLE_STATE_ALTERNATE;
        config.loop = true;
    } else {
        animation = new VerticalChaseAnimation();
        config.speed = 5;
        config.curve = EaseCurve::ELASTIC;
        config.initialState = BOUNTY_IDLE_STATE;
        config.loopDelayMs = 1500;
        config.loop = true;
    }
    pdn->getLightManager()->startAnimation(animation, config);   // driver call

    parameterizedCallbackFunction cycleStats = [](void* ctx) {
        Idle* idle = (Idle*)ctx;
        idle->statsIndex++;
        if (idle->statsIndex > idle->statsCount) idle->statsIndex = 0;
        idle->displayIsDirty = true;                            // manual dirty flag
    };
    pdn->getPrimaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);  // driver call
    pdn->getSecondaryButton()->setButtonPress(cycleStats, this, ButtonInteraction::CLICK);

    displayIsDirty = true;
}

void Idle::onStateLoop(PDN* pdn) {
    if (displayIsDirty) {
        renderStats(pdn);        // direct display driver calls inside
        displayIsDirty = false;
    }
    // ... business logic ...
}

void Idle::onStateDismounted(PDN* pdn) {
    statsIndex = 0;
    matchInitializationTimer.invalidate();
    matchInitialized = false;
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);             // driver call
    pdn->getPrimaryButton()->removeButtonCallbacks();            // driver call
    pdn->getSecondaryButton()->removeButtonCallbacks();          // driver call
    transitionToSymbolState = false;
}

void Idle::renderStats(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()->drawImage(...);                           // driver call
    pdn->getDisplay()->drawText("Wins", 74, 20);                // driver call
    pdn->getDisplay()->render();                                 // driver call
}
```

---

## idle-state.cpp — after

```cpp
void Idle::onStateMounted(PDN* pdn) {
    pdn->getRadioManager()->enablePeerCommsMode();

    AnimationConfig config;
    AnimationBase* animation;
    if (player->isHunter()) {
        animation = new IdleAnimation();
        config.speed = 16;
        config.curve = EaseCurve::LINEAR;
        config.initialState = HUNTER_IDLE_STATE_ALTERNATE;
        config.loop = true;
    } else {
        animation = new VerticalChaseAnimation();
        config.speed = 5;
        config.curve = EaseCurve::ELASTIC;
        config.initialState = BOUNTY_IDLE_STATE;
        config.loopDelayMs = 1500;
        config.loop = true;
    }

    parameterizedCallbackFunction cycleStats = [](void* ctx) {
        Idle* idle = (Idle*)ctx;
        idle->statsIndex++;
        if (idle->statsIndex > idle->statsCount) idle->statsIndex = 0;
        idle->updateStatsDisplay();   // calls applyRenderState with display-only delta
    };

    // Describe full initial state in one call
    applyRenderState({
        .leds  = LedModule { AnimationDescriptor { animation, config } },
        .input = InputConfig {
            .primaryButton   = ButtonConfig { CommandId::NONE, { cycleStats, this } },
            .secondaryButton = ButtonConfig { CommandId::NONE, { cycleStats, this } }
        }
    });

    // Initial display content (separate call — merges into the same pendingDelta)
    updateStatsDisplay();
}

void Idle::onStateLoop(PDN* pdn) {
    // Pure business logic — no driver calls, no dirty flag management
    // ... match init, symbol transition checks, etc. ...
}

void Idle::onStateDismounted(PDN* pdn) {
    // Pure cleanup — no driver calls
    statsIndex = 0;
    matchInitializationTimer.invalidate();
    matchInitialized = false;
    transitionToSymbolState = false;
    // clearFrame() has already been called by StateMachine before this runs
}

// isPreemptable() — Idle is interruptible, default true inherited from State

// Replaces renderStats() — no PDN* parameter, no driver calls
void Idle::updateStatsDisplay() {
    DisplayModule display;
    display.icon = getImageForAllegiance(player->getAllegiance(), ImageType::IDLE);

    if (statsIndex == 0) {
        display.text = "Wins\n" + std::to_string(player->getWins());
    } else if (statsIndex == 1) {
        display.text = "Streak\n" + std::to_string(player->getStreak());
    } else if (statsIndex == 2) {
        display.text = "Losses\n" + std::to_string(player->getLosses());
    }
    // ... etc ...

    // Display-only delta — leds and input are null, animation keeps running
    applyRenderState({ .display = display });
}
```

**What disappeared:** All driver calls in mount and dismount. `displayIsDirty` flag. `renderStats(PDN*)` with its driver calls. `removeButtonCallbacks()` in dismount.

**What the two `applyRenderState` calls in `onStateMounted` do:** They merge into the same `pendingDelta`. The state machine sees one `frameDirty` at the end of the tick and calls `renderEngine->apply()` once with the combined delta (leds + input + display).

---

## duel-state.cpp — before

```cpp
void Duel::onStateMounted(PDN* pdn) {
    chainDuelManager->sendGameEventToSupporters(ChainGameEventType::DRAW);
    matchManager->setDuelLocalStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    auto duelButtonPush = matchManager->getDuelButtonPush();
    pdn->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);  // driver call
    pdn->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);

    duelTimer.setTimer(DUEL_TIMEOUT);

    pdn->getDisplay()->invalidateScreen()                                                             // driver call
        ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))
        ->drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::DRAW))
        ->render();

    AnimationConfig config;
    config.speed = 16;
    config.loop = false;
    config.initialState = COUNTDOWN_DUEL_STATE;
    pdn->getLightManager()->startAnimation(new CountdownAnimation(), config);                        // driver call

    pdn->getHaptics()->setIntensity(175);                                                            // driver call
}

void Duel::onStateDismounted(PDN* pdn) {
    if (transitionToIdleState || transitionToShootoutSpectatorState
        || transitionToShootoutEliminatedState) {
        pdn->getHaptics()->off();                            // driver call
        matchManager->clearCurrentMatch();
        pdn->getPrimaryButton()->removeButtonCallbacks();    // driver call
        pdn->getSecondaryButton()->removeButtonCallbacks();  // driver call
    } else if (transitionToDuelReceivedResultState) {
        pdn->getPrimaryButton()->removeButtonCallbacks();    // driver call
        pdn->getSecondaryButton()->removeButtonCallbacks();  // driver call
    }

    duelTimer.invalidate();
    transitionToDuelReceivedResultState = false;
    transitionToIdleState = false;
    transitionToDuelPushedState = false;
    transitionToShootoutSpectatorState = false;
    transitionToShootoutEliminatedState = false;
}
```

---

## duel-state.cpp — after

```cpp
void Duel::onStateMounted(PDN* pdn) {
    // Business logic unchanged
    chainDuelManager->sendGameEventToSupporters(ChainGameEventType::DRAW);
    matchManager->setDuelLocalStartTime(SimpleTimer::getPlatformClock()->milliseconds());
    duelTimer.setTimer(DUEL_TIMEOUT);

    auto duelButtonPush = matchManager->getDuelButtonPush();

    AnimationConfig config;
    config.speed = 16;
    config.loop = false;
    config.initialState = COUNTDOWN_DUEL_STATE;

    // Describe full initial state in one call
    applyRenderState({
        .display = DisplayModule {
            .icon = getImageForAllegiance(player->getAllegiance(), ImageType::DRAW)
        },
        .leds = LedModule { AnimationDescriptor { new CountdownAnimation(), config } },
        .input = InputConfig {
            .primaryButton   = ButtonConfig { CommandId::DRAW, { duelButtonPush, matchManager } },
            .secondaryButton = ButtonConfig { CommandId::DRAW, { duelButtonPush, matchManager } }
        },
        .haptics = HapticModule { .intensity = 175 }
    });
}

void Duel::onStateLoop(PDN* pdn) {
    duelTimer.updateTime();
    // Pure business logic — transition flags, timer checks
    // Identical to before, no driver calls
}

void Duel::onStateDismounted(PDN* pdn) {
    // Conditional business logic only — NO driver calls
    if (transitionToIdleState || transitionToShootoutSpectatorState
        || transitionToShootoutEliminatedState) {
        matchManager->clearCurrentMatch();
        // haptics off, button removal: clearFrame() handles unconditionally
    }

    duelTimer.invalidate();
    transitionToDuelReceivedResultState = false;
    transitionToIdleState = false;
    transitionToDuelPushedState = false;
    transitionToShootoutSpectatorState = false;
    transitionToShootoutEliminatedState = false;
}

// isPreemptable() — Duel is the timing-critical draw window; block MC pushes
bool Duel::isPreemptable() const override {
    return false;
}
```

**What disappeared:** Every driver call in mount. All conditional callback removal and haptic teardown in dismount — `clearFrame()` runs unconditionally on every transition, so the conditional branches that existed solely to call `removeButtonCallbacks()` collapse entirely. Only the conditional `clearCurrentMatch()` business logic survives.

**`onStateMounted` is now a pure description:** it reads like a spec of what the duel state looks like — draw image, countdown animation, both buttons fire the draw callback, haptics on. No sequencing of driver calls, no teardown concerns.

---

## Summary of the pattern

| Before | After |
|---|---|
| `pdn->getLightManager()->startAnimation(anim, cfg)` | `applyRenderState({ .leds = LedModule { AnimationDescriptor { anim, cfg } } })` |
| `pdn->getDisplay()->drawText(...)->render()` | `applyRenderState({ .display = DisplayModule { text, icon } })` |
| `pdn->getPrimaryButton()->setButtonPress(cb, ctx, CLICK)` | `applyRenderState({ .input = InputConfig { .primaryButton = { cmdId, { cb, ctx } } } })` |
| `pdn->getPrimaryButton()->removeButtonCallbacks()` | *(removed — clearFrame() handles it)* |
| `displayIsDirty = true` then `if (displayIsDirty) renderX(pdn)` | `applyRenderState({ .display = ... })` ← done |
| `pdn->getHaptics()->off()` in dismount | *(removed — clearFrame() handles it)* |
| `pdn->getDisplay()->setGlyphMode(...)` in dismount | *(removed — clearFrame() handles it)* |
| Multiple driver calls scattered across mount/loop/dismount | One `applyRenderState` call per logical state change |

## The merge model — two calls in one tick

```cpp
// Both of these in the same tick compose correctly:
applyRenderState({ .display = newDisplay });  // pendingDelta.display = newDisplay
applyRenderState({ .leds = newLeds });        // pendingDelta.leds = newLeds
// → render engine receives { .display = newDisplay, .leds = newLeds } in one apply() call
// → animation is not restarted; display is updated
```
