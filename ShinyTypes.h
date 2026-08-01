#ifndef __SHINY_TYPES__H
#define __SHINY_TYPES__H
#include "FastLED.h"
#include <vector>

enum RunMode
{
    Off = 0,
    On = 1,

    RunModeCount
};

#define LAYER_COUNT 10
#define PRESET_COUNT 5
#define MAX_LED_COUNT 800

enum LayerBlendMode
{
    BlendModeAdd,
    BlendModeSubtract,
    BlendModeAddWrap,
    BlendModeSubtractWrap,
    BlendModeMultiply,
    BlendModeDissolve,
    BlendModeAverage,
    BlendModeSet,
    BlendModeScreen,
    BlendModeLighten,
    BlendModeDarken,
    BlendModeDifference,
    BlendModeOverlay,
    BlendModeColorDodge,

    BlendModeCount
};
extern std::vector<String> blendModeNames;

enum LedColorOrder
{
    LedOrderRGB,
    LedOrderGRB,
    LedOrderBGR,

    LedOrderCount
};
extern std::vector<String> ledColorOrderNames;

// These initializers are the defaults, everywhere: what a property reverts to
// on an empty write, and what an unstored key reads as.
struct ShinyLayerSettings
{
    CRGB mainColor = CRGB(255, 100, 0);
    CRGB secondaryColor = CRGB(240, 255, 0);
    LayerBlendMode blendMode = BlendModeAdd;
    float speed = 1.0;
    float p_tau = 10.0;
    float p_phi = 4.0;
    int animationIndex = 0;
    // When set, speed changes unit from seconds per cycle to beats per cycle,
    // and the animation phase-locks to the beat grid: 1 = a cycle per beat,
    // 4 = a cycle per bar, 0.5 = twice per beat.
    int beatSync = 0;
};

// NVS key for a per-layer setting. Preset 0 uses the unsuffixed form that the
// pre-preset firmware wrote, so upgrading a core keeps its settings.
String layerKey(const String &key, int layer, int preset);

struct ShinySettings
{
    RunMode mode = On;
    int brightness = 255;
    ShinyLayerSettings layers[LAYER_COUNT];
    int currentLayerIndex = 0;
    int currentPresetIndex = 0;
    LedColorOrder ledColorOrder = LedOrderGRB;
    int ledCount = MAX_LED_COUNT/2;
#ifdef ARDUINO_M5STACK_ATOM_ECHO
    int micEnabled = 1; // an Echo's whole point is its microphone
#else
    int micEnabled = 0; // only useful on an M5Atom Echo; see BeatDetector.h
#endif
    int meshEnabled = 1; // sync with nearby shinercores; see Mesh.h
    int meshShow = 1;    // play neighbors' presets in the shared carousel
    int carouselBeats = 8; // beats each carousel slot plays for
    ShinyLayerSettings *currentLayer()
    {
        return &layers[currentLayerIndex];
    }
};

#endif
