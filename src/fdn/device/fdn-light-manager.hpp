#pragma once

#include "device/light-manager.hpp"

// FDN physical LED layout:
//   DISPLAY_LIGHTS → recess strip  (22 LEDs, indices 0-21)
//   GRIP_LIGHTS    → fin strip     (9 LEDs,  indices 0-8)
//
// Mapping from LEDState (leftLights[9] / rightLights[9]):
//   Fin  (GRIP_LIGHTS) 0-8  → leftLights[0-8]
//   Recess (DISPLAY_LIGHTS) 0-8  → rightLights[0-8]
//   Recess 9-17 are left dark (not enough LEDState entries for all 22).
//
// For explicit half-screen effects (e.g. match-side lights) call the
// stripSetLight() helpers below which write directly to the LightStrip.
class FDNLightManager : public LightManager {
public:
    explicit FDNLightManager(LightStrip& strip)
        : LightManager(strip) {}

    ~FDNLightManager() override = default;

    // Direct LightStrip access for patterns that need more than 18 LEDs.
    void setRecessLight(uint8_t index, LEDColor color, uint8_t brightness) {
        pdnLights.setLight(
            LightIdentifier::DISPLAY_LIGHTS,
            index,
            LEDState::SingleLEDState(color, brightness));
    }

    void setFinLight(uint8_t index, LEDColor color, uint8_t brightness) {
        pdnLights.setLight(
            LightIdentifier::GRIP_LIGHTS,
            index,
            LEDState::SingleLEDState(color, brightness));
    }

    void clearAllLights(uint8_t numRecess, uint8_t numFin) {
        LEDState::SingleLEDState off;
        for (uint8_t i = 0; i < numRecess; ++i) {
            pdnLights.setLight(LightIdentifier::DISPLAY_LIGHTS, i, off);
        }
        for (uint8_t i = 0; i < numFin; ++i) {
            pdnLights.setLight(LightIdentifier::GRIP_LIGHTS, i, off);
        }
    }

protected:
    // Map LEDState entries to FDN's physical strips.
    //   leftLights[0-8]  → fin lights  0-8  (GRIP_LIGHTS)
    //   rightLights[0-8] → recess lights 0-8 (DISPLAY_LIGHTS)
    void applyLEDState(const LEDState& state) override {
        for (uint8_t i = 0; i < 9; ++i) {
            pdnLights.setLight(LightIdentifier::GRIP_LIGHTS,    i, state.leftLights[i]);
            pdnLights.setLight(LightIdentifier::DISPLAY_LIGHTS, i, state.rightLights[i]);
        }
    }
};
