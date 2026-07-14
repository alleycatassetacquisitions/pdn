#pragma once

#include <map>
#include "images-raw.hpp"
#include "image.hpp"
#include "device/drivers/display.hpp"
#include "device/drivers/light-interface.hpp"
#include "game/player.hpp"

typedef std::map<ImageType, Image> ImageCollection;

inline const ImageCollection alleycatImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_logo_alley, 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(image_logo_alley, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_alley_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_alley_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_alley_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_alley_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_alley_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_alley_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_draw, 128, 64, 64, 0)},
{ImageType::WIN, Image(image_alley_victor, 64, 64, 64, 0)},
{ImageType::LOSE, Image(image_alley_loser, 64, 64, 64, 0)},
};

inline const ImageCollection helixImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_logo_helix, 128, 64, 64, 0)},
    {ImageType::LOGO_LEFT, Image(image_logo_helix, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_helix_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_helix_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_helix_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_helix_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_helix_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_helix_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_draw, 128, 64, 64, 0)},
{ImageType::WIN, Image(image_helix_victor, 64, 64, 64, 0)},
{ImageType::LOSE, Image(image_helix_loser, 64, 64, 64, 0)},
};

inline const ImageCollection endlineImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_logo_endline, 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(image_logo_endline, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_endline_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_endline_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_endline_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_endline_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_endline_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_endline_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_draw, 128, 64, 64, 0)},
{ImageType::WIN, Image(image_endline_victor, 64, 64, 64, 0)},
{ImageType::LOSE, Image(image_endline_loser, 64, 64, 64, 0)},
};

inline const ImageCollection resistanceImageCollection = {
    {ImageType::LOGO_RIGHT, Image(image_resistance_stamp, 128, 64, 64, 0)},
{ImageType::LOGO_LEFT, Image(image_resistance_stamp, 128, 64, 0, 0)},
{ImageType::IDLE, Image(image_resistance_0, 128, 64, 0, 0)},
{ImageType::STAMP, Image(image_resistance_stamp, 128, 64, 64, 0)},
{ImageType::CONNECT, Image(image_resistance_connect, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_THREE, Image(image_resistance_count3, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_TWO, Image(image_resistance_count2, 128, 64, 0, 0)},
{ImageType::COUNTDOWN_ONE, Image(image_resistance_count1, 128, 64, 0, 0)},
{ImageType::DRAW, Image(image_draw, 128, 64, 64, 0)},
{ImageType::WIN, Image(image_resistance_victor, 64, 64, 64, 0)},
{ImageType::LOSE, Image(image_resistance_loser, 64, 64, 64, 0)},
};

// Equivalent LEDColor palettes (derived from FastLED HTML colors in crgb.h):
// - CRGB::Red       = #FF0000 = (255, 0, 0)
// - CRGB::Orange    = #FFA500 = (255, 165, 0)
// - CRGB::DarkGreen = #006400 = (0, 100, 0)
// - CRGB::DarkBlue  = #00008B = (0, 0, 139)
// - CRGB::Yellow    = #FFFF00 = (255, 255, 0)
inline const LEDColor bountyColors[16] = {
    LEDColor(255, 0, 0),   LEDColor(255, 0, 0),   LEDColor(255, 0, 0),   LEDColor(255, 165, 0),
    LEDColor(255, 0, 0),   LEDColor(255, 0, 0),   LEDColor(255, 0, 0),   LEDColor(255, 165, 0),
    LEDColor(255, 165, 0), LEDColor(255, 0, 0),   LEDColor(255, 0, 0),   LEDColor(255, 0, 0),
    LEDColor(255, 165, 0), LEDColor(255, 0, 0),   LEDColor(255, 0, 0),   LEDColor(255, 0, 0),
};

inline const LEDColor hunterColors[16] = {
    LEDColor(0, 100, 0), LEDColor(0, 100, 0), LEDColor(0, 100, 0), LEDColor(0, 0, 139),
    LEDColor(0, 100, 0), LEDColor(0, 100, 0), LEDColor(0, 100, 0), LEDColor(0, 0, 139),
    LEDColor(0, 0, 139), LEDColor(0, 100, 0), LEDColor(0, 100, 0), LEDColor(0, 100, 0),
    LEDColor(0, 0, 139), LEDColor(0, 100, 0), LEDColor(0, 100, 0), LEDColor(0, 100, 0),
};

inline const LEDColor debugColors[16] = {
    LEDColor(0, 100, 0),   LEDColor(0, 0, 139),   LEDColor(0, 100, 0),   LEDColor(0, 0, 139),
    LEDColor(255, 0, 0),   LEDColor(255, 255, 0), LEDColor(255, 0, 0),   LEDColor(255, 255, 0),
    LEDColor(0, 100, 0),   LEDColor(0, 0, 139),   LEDColor(0, 100, 0),   LEDColor(0, 0, 139),
    LEDColor(255, 0, 0),   LEDColor(255, 255, 0), LEDColor(255, 0, 0),   LEDColor(255, 255, 0),
};

// Idle animation colors for bounty and hunter
inline const LEDColor bountyIdleColors[4] = {
    LEDColor(255,2,1),    // Color 1
    LEDColor(255,51,0),   // Color 3
    LEDColor(237,75,0),   // Color 2 (7% dimmer)
    LEDColor(222,97,7)    // Color 4 (13% dimmer)
};

inline const LEDColor hunterIdleColors[4] = {
    LEDColor(0,255,0),    // Green
    LEDColor(0,237,75),   // Blue-green
    LEDColor(0,200,100),  // Teal
    LEDColor(0,180,180)   // Cyan
};

// LED color constants converted from CRGB to LEDColor
inline const LEDColor bountyIdleLEDColors[8] = {
    LEDColor(255, 2, 1),     // Color 1
    LEDColor(237, 75, 0),    // Color 2 (7% dimmer)
    LEDColor(255, 51, 0),    // Color 3
    LEDColor(222, 97, 7),     // Color 4 (13% dimmer)
    LEDColor(255, 2, 1),     // Color 1
    LEDColor(237, 75, 0),    // Color 2 (7% dimmer)
    LEDColor(255, 51, 0),    // Color 3
    LEDColor(222, 97, 7)     // Color 4 (13% dimmer)
};

inline const LEDColor hunterIdleLEDColors[8] = {
    LEDColor(0, 255, 0),     // Green
    LEDColor(0, 237, 75),    // Blue-green
    LEDColor(0, 200, 100),   // Teal
    LEDColor(0, 180, 180),    // Cyan
    LEDColor(0, 255, 0),     // Green
    LEDColor(0, 237, 75),    // Blue-green
    LEDColor(0, 200, 100),   // Teal
    LEDColor(0, 180, 180)    // Cyan
};

inline const LEDColor hunterIdleLEDColorsAlternate[9] = {
    LEDColor(0, 255, 0),     // Green
    LEDColor(0, 237, 75),    // Blue-green
    LEDColor(0, 200, 100),   // Teal
    LEDColor(0, 180, 180),    // Cyan
    LEDColor(0, 255, 0),     // Green
    LEDColor(0, 237, 75),    // Blue-green
    LEDColor(0, 200, 100),   // Teal
    LEDColor(0, 180, 180),    // Cyan
    LEDColor(0, 180, 180)    // Cyan
};

inline const LEDColor bountyIdleLEDColorsAlternate[9] = {
    LEDColor(255, 2, 1),     // Color 1
    LEDColor(237, 75, 0),    // Color 2 (7% dimmer)
    LEDColor(255, 51, 0),    // Color 3
    LEDColor(222, 97, 7),     // Color 4 (13% dimmer)
    LEDColor(255, 2, 1),     // Color 1
    LEDColor(237, 75, 0),    // Color 2 (7% dimmer)
    LEDColor(255, 51, 0),    // Color 3
    LEDColor(222, 97, 7),     // Color 4 (13% dimmer)
    LEDColor(0, 0, 0),    // Color 2 (7% dimmer)
};


inline const LEDState HUNTER_IDLE_STATE = [](){
    LEDState state;
    // Initialize with default values (all black)
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
    }
    state.transmitLight = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
    
    // Set the first 4 LEDs with hunter colors at full brightness
    for (int i = 0; i < 4; i++) {
        if (i < 4) {
            state.leftLights[i] = LEDState::SingleLEDState(hunterIdleLEDColors[i], 255);
            state.rightLights[i] = LEDState::SingleLEDState(hunterIdleLEDColors[i], 255);
        }
    }
    
    return state;
}();

inline const LEDState HUNTER_IDLE_STATE_ALTERNATE = [](){
    LEDState state;
    
    // Set the first 4 LEDs with hunter colors at full brightness
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(hunterIdleLEDColorsAlternate[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(hunterIdleLEDColorsAlternate[i], 65);
    }
    
    return state;
}();

// Bounty idle LED state - red/orange gradient
inline const LEDState BOUNTY_IDLE_STATE = [](){
    LEDState state;
    // Initialize with default values (all black)
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
    }
    state.transmitLight = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
    
    // Set the first 4 LEDs with bounty colors at full brightness
    for (int i = 0; i < 4; i++) {
        if (i < 4) {
            state.leftLights[i] = LEDState::SingleLEDState(bountyIdleLEDColors[i], 255);
            state.rightLights[i] = LEDState::SingleLEDState(bountyIdleLEDColors[i], 255);
        }
    }
    
    return state;
}();

inline const LEDState BOUNTY_IDLE_STATE_ALTERNATE = [](){
    LEDState state;
    
    state.transmitLight = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
    
    for (int i = 0; i < 9; i++) { 
        state.leftLights[i] = LEDState::SingleLEDState(bountyIdleLEDColorsAlternate[i], 65);
        state.rightLights[i] = LEDState::SingleLEDState(bountyIdleLEDColorsAlternate[i], 65);
    }
    
    return state;
}();

// Test/debug player IDs used by player-registration to bypass network auth.
inline const std::string TEST_BOUNTY_ID     = "9999";
inline const std::string TEST_HUNTER_ID     = "8888";
inline const std::string BROADCAST_WIFI     = "1111";
inline const std::string FORCE_MATCH_UPLOAD = "6969";

// Easing curve lookup tables have moved to lib/core/include/utils/easing-curves.hpp.
#include "utils/easing-curves.hpp"

static const ImageCollection* getCollectionForAllegiance(Allegiance allegiance) {
    switch(allegiance) {
        case Allegiance::HELIX:
            return &helixImageCollection;
        case Allegiance::ENDLINE:
            return &endlineImageCollection;
        case Allegiance::RESISTANCE:
            return &resistanceImageCollection;
        default:
            return &alleycatImageCollection;
    }
}

static Image getImageForAllegiance(Allegiance allegiance, ImageType whichImage) {
    return getCollectionForAllegiance(allegiance)->at(whichImage);
}

static const char* digitGlyphs[] = {
    "\u0030",
    "\u0031",
    "\u0032",
    "\u0033",
    "\u0034",
    "\u0035",
    "\u0036",
    "\u0037",
    "\u0038",
    "\u0039",
    "\u0040"
};

static const char* loadingGlyphs[] = {
    "\u2630",
    "\u2631",
    "\u2632",
    "\u2633",
    "\u2634",
    "\u2635",
    "\u2636",
    "\u2637"
};

static void renderLoadingScreen(Display* display) {
    const int GLYPH_SIZE = 14;
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;

    const int GLYPHS_PER_ROW = (SCREEN_WIDTH / GLYPH_SIZE);
    const int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE / GLYPH_SIZE);

    display->invalidateScreen();
    display->setGlyphMode(FontMode::LOADING_GLYPH);

    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if (rand() % 100 < 50) {
                int x = col * GLYPH_SIZE;
                int y = 14 + (row * GLYPH_SIZE);
                int randomIndex = rand() % 8;
                const char* glyph = loadingGlyphs[randomIndex];
                display->renderGlyph(glyph, x, y);
            }
        }
    }

    display->render();
}

#include "game/quickdraw-countdown.hpp"

static const LEDState& COUNTDOWN_THREE_STATE = QuickdrawCountdown::threeLedState();
static const LEDState& COUNTDOWN_TWO_STATE = QuickdrawCountdown::twoLedState();
static const LEDState& COUNTDOWN_ONE_STATE = QuickdrawCountdown::oneLedState();
static const LEDState& COUNTDOWN_DUEL_STATE = QuickdrawCountdown::drawLedState();