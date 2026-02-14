#pragma once
#include <cstdint>

enum class LightIdentifier {
    GLOBAL = 0,
    DISPLAY_LIGHTS = 1,
    GRIP_LIGHTS = 2,
    TRANSMIT_LIGHT = 3,
    LEFT_LIGHTS = 4,
    RIGHT_LIGHTS = 5
};

struct LEDColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    constexpr LEDColor(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) : red(r), green(g), blue(b) {}
};

// New struct to represent the state of all LEDs
struct LEDState {
    struct SingleLEDState {
        LEDColor color;
        uint8_t brightness;
        SingleLEDState() : color(0,0,0), brightness(0) {}
        SingleLEDState(LEDColor c, uint8_t b) : color(c), brightness(b) {}
    };

    SingleLEDState leftLights[9];   // 9 LEDs for left side (0-8)
    SingleLEDState rightLights[9];  // 9 LEDs for right side (0-8)
    SingleLEDState transmitLight;   // The transmit LED
    uint32_t timestamp;            // When this state was generated

    LEDState() : timestamp(0) {}
    
    // Helper to set a single LED in the state
    void setLED(bool isLeft, uint8_t index, const LEDColor& color, uint8_t brightness) {
        if (index >= 9) return;
        
        SingleLEDState& led = isLeft ? 
            leftLights[index] : 
            rightLights[index];
            
        led.color = color;
        led.brightness = brightness;
    }

    // Helper to set both left and right LEDs
    void setLEDPair(uint8_t index, const LEDColor& color, uint8_t brightness) {
        setLED(true, index, color, brightness);
        setLED(false, index, color, brightness);
    }

    // Helper to clear all LEDs
    void clear() {
        for (uint8_t i = 0; i < 9; i++) {
            leftLights[i] = SingleLEDState();
            rightLights[i] = SingleLEDState();
        }
        transmitLight = SingleLEDState();
    }
};

class LightStrip {
public:
    virtual ~LightStrip() = default;
    virtual void setLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) = 0;
    virtual void setLightBrightness(LightIdentifier lightSet, uint8_t index, uint8_t brightness) = 0;
    virtual void setGlobalBrightness(uint8_t brightness) = 0;
    virtual LEDState::SingleLEDState getLight(LightIdentifier lightSet, uint8_t index) = 0;
    virtual void fade(LightIdentifier lightSet, uint8_t fadeAmount) = 0;
    virtual void addToLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) = 0;
    virtual void setFPS(uint8_t fps) = 0;
    virtual uint8_t getFPS() const = 0;
};

enum class AnimationType {
    IDLE,
    VERTICAL_CHASE,
    DEVICE_CONNECTED,
    COUNTDOWN,
    LOSE,
    HUNTER_WIN,
    BOUNTY_WIN,
    TRANSMIT_BREATH
};

enum class EaseCurve {
    LINEAR,
    EASE_IN_OUT,
    EASE_OUT,
    ELASTIC
};

struct AnimationConfig {
    AnimationType type;
    bool loop;
    uint8_t speed;
    EaseCurve curve = EaseCurve::LINEAR;  // Default to linear curve
    LEDState initialState = LEDState();                // Initial LED state for the animation
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