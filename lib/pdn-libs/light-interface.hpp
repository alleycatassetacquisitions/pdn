#pragma once
#include <cstdint>

// Forward declarations of types to keep interface clean
enum class LightIdentifier {
    GLOBAL = 0,
    DISPLAY_LIGHTS = 1,
    GRIP_LIGHTS = 2,
    TRANSMIT_LIGHT = 3,
    LEFT_LIGHTS = 4,
    RIGHT_LIGHTS = 5
};

struct LEDColor {
    int red;
    int green;
    int blue;

    LEDColor(int r = 0, int g = 0, int b = 0) : red(r), green(g), blue(b) {}
};

enum class AnimationType {
    IDLE,
    VERTICAL_CHASE,
    DEVICE_CONNECTED,
    COUNTDOWN,
    LOSE,
    WIN
};

enum class EaseCurve {
    LINEAR,
    EASE_IN_OUT,
    EASE_OUT,
    ELASTIC
};

// New struct to represent the state of all LEDs
struct LEDState {
    struct SingleLEDState {
        LEDColor color;
        uint8_t brightness;
        bool reserved;  // If true, this LED should not be modified by animations
        SingleLEDState() : color(0,0,0), brightness(0), reserved(false) {}
        SingleLEDState(LEDColor c, uint8_t b) : color(c), brightness(b), reserved(false) {}
        SingleLEDState(LEDColor c, uint8_t b, bool r) : color(c), brightness(b), reserved(r) {}
    };

    SingleLEDState leftLights[9];   // 9 LEDs for left side (0-8)
    SingleLEDState rightLights[9];  // 9 LEDs for right side (0-8)
    SingleLEDState transmitLight;   // The transmit LED
    uint32_t timestamp;            // When this state was generated

    LEDState() : timestamp(0) {}
    
    // Helper to check if an LED is reserved
    bool isReserved(bool isLeft, uint8_t index) const {
        if (index >= 9) return false;
        return isLeft ? leftLights[index].reserved : rightLights[index].reserved;
    }
    
    // Helper to set a single LED in the state
    void setLED(bool isLeft, uint8_t index, const LEDColor& color, uint8_t brightness, bool reserved = false) {
        if (index >= 9) return;
        
        SingleLEDState& led = isLeft ? 
            leftLights[index] : 
            rightLights[index];
            
        led.color = color;
        led.brightness = brightness;
        led.reserved = reserved;
    }

    // Helper to set both left and right LEDs, ignoring reserved status
    void setLEDPair(uint8_t index, const LEDColor& color, uint8_t brightness, bool reserved = false) {
        setLED(true, index, color, brightness, reserved);
        setLED(false, index, color, brightness, reserved);
    }

    // Helper to set both left and right LEDs, respecting reserved status
    void setLEDPairRespectingReserved(uint8_t index, const LEDColor& color, uint8_t brightness, bool forceUpdate = false) {
        if (forceUpdate || !isReserved(true, index)) {
            setLED(true, index, color, brightness);
        }
        if (forceUpdate || !isReserved(false, index)) {
            setLED(false, index, color, brightness);
        }
    }

    // Helper to clear all non-reserved LEDs
    void clear() {
        for (int i = 0; i < 9; i++) {
            if (!leftLights[i].reserved) {
                leftLights[i] = SingleLEDState();
            }
            if (!rightLights[i].reserved) {
                rightLights[i] = SingleLEDState();
            }
        }
        if (!transmitLight.reserved) {
            transmitLight = SingleLEDState();
        }
    }
};

struct AnimationConfig {
    AnimationType type;
    bool loop;
    uint8_t speed;
    EaseCurve curve = EaseCurve::LINEAR;  // Default to linear curve
    LEDState initialState;                // Initial LED state for the animation
    uint16_t loopDelayMs = 0;             // Delay between animation loops (in milliseconds)
};

// Interface for animations
class IAnimation {
public:
    virtual ~IAnimation() = default;
    
    // Initialize or reset the animation with a configuration
    virtual void init(const AnimationConfig& config) = 0;
    
    // Generate the next frame of the animation
    // Returns: The complete LED state for this frame
    virtual LEDState animate() = 0;
    
    // Check if the animation has completed
    virtual bool isComplete() const = 0;
    
    // Pause the animation
    virtual void pause() = 0;
    
    // Resume the animation
    virtual void resume() = 0;
    
    // Stop the animation
    virtual void stop() = 0;
    
    // Get the current animation type
    virtual AnimationType getType() const = 0;
    
    // Check if the animation is paused
    virtual bool isPaused() const = 0;

protected:
    // Helper method to get easing values
    virtual uint8_t getEasingValue(uint8_t progress, EaseCurve curve) const = 0;
    
    // Helper method to interpolate between two colors
    virtual LEDColor interpolateColor(const LEDColor& start, const LEDColor& end, uint8_t t) const = 0;
}; 