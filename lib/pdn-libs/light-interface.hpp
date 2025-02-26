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

struct AnimationConfig {
    AnimationType type;
    bool loop;
    uint8_t speed;
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
};

// Interface for animations
class IAnimation {
public:
    virtual ~IAnimation() = default;
    
    // Initialize or reset the animation with a configuration
    virtual void init(const AnimationConfig& config) = 0;
    
    // Generate the next frame of the animation
    // Returns: The complete LED state for this frame
    virtual LEDState animate(uint32_t currentTime) = 0;
    
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
};

class ILightController {
public:
    virtual ~ILightController() = default;

    // Core functionality
    virtual void begin() = 0;
    virtual void loop() = 0;
    
    // Animation control
    virtual void startAnimation(AnimationConfig config) = 0;
    virtual void stopAnimation() = 0;
    virtual void pauseAnimation() = 0;
    virtual void resumeAnimation() = 0;
    
    // Direct LED control
    virtual void setLightColor(LightIdentifier lights, uint8_t index, LEDColor color) = 0;
    virtual void setAllLights(LightIdentifier lights, LEDColor color) = 0;
    virtual void setBrightness(LightIdentifier lights, uint8_t brightness) = 0;
    virtual void clear(LightIdentifier lights) = 0;
    
    // Color palette management
    virtual void setPalette(const LEDColor* colors, uint8_t numColors) = 0;
    virtual LEDColor getPaletteColor(uint8_t index) const = 0;
    
    // Animation state query
    virtual bool isAnimating() const = 0;
    virtual bool isPaused() const = 0;
    virtual AnimationType getCurrentAnimation() const = 0;

protected:
    // Helper methods that implementations might want to use
    virtual uint8_t getEasingValue(uint8_t progress, EaseCurve curve) const = 0;
    virtual LEDColor interpolateColor(const LEDColor& start, const LEDColor& end, uint8_t t) const = 0;
}; 