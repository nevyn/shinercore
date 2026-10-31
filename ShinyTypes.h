#ifndef __SHINY_TYPES__H
#define __SHINY_TYPES__H
#include "FastLED.h"
#include <vector>
#include "Wiring.h"
#include "src/render/Render.h"

enum RunMode
{
    Off = 0,
    On = 1,

    RunModeCount
};

#define LAYER_COUNT 10
#define PRESET_COUNT 5
#define MAX_LED_COUNT 800

// Derived from the render library's tables
extern std::vector<String> blendModeNames;
extern std::vector<String> animationNames;
// The `animationDocs` characteristic's value (docs/ble-comms.md): each animation's
// color use and parameter names, from the render library's table
String animationDocsJSON();

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
    float opacity = 1.0; // how strongly the layer composites onto the layers below, 0-1
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
    int bootPreset = -1; // the power-on preset, which also turns the lights on; -1 resumes as left
    LedColorOrder ledColorOrder = LedOrderGRB;
    int ledCount = MAX_LED_COUNT/2;
    // Battery budget across both strip outputs, 0 = uncapped. A hot frame dims
    // instead of browning out the pack; set high enough that it only catches spikes.
    int maxCurrentMa = 0;
#ifdef ARDUINO_M5STACK_ATOM_ECHO
    int micEnabled = 1; // an Echo's whole point is its microphone
#else
    int micEnabled = 0; // only useful on an M5Atom Echo; see BeatDetector.h
#endif
    int meshEnabled = 0; // sync with nearby shinercores; see Mesh.h
    int meshShow = 1;    // play neighbors' presets in the shared carousel
    String meshGroup;    // only mesh with cores in the same group; "" is a group too
    int carouselBeats = 8; // beats each carousel slot plays for
    Wiring wiring = defaultWiring(ACTIVE_BOARD); // pin roles; see docs/wiring.md
    ShinyLayerSettings *currentLayer()
    {
        return &layers[currentLayerIndex];
    }
};

#endif
