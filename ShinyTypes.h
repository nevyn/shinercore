#ifndef __SHINY_TYPES__H
#define __SHINY_TYPES__H
#include "FastLED.h"

enum RunMode
{
    Off = 0,
    On = 1,

    RunModeCount
};
void setMode(RunMode newMode);

#define LAYER_COUNT 10

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

struct ShinyLayerSettings
{
    CRGB mainColor = CRGB(255, 100, 0);
    CRGB secondaryColor = CRGB(240, 255, 0);
    LayerBlendMode blendMode = BlendModeAdd;
    float speed = 2.0;
    float p_tau = 10.0;
    float p_phi = 4.0;
    int animationIndex = 0;
};

void setLayer(int newLayer);

struct ShinySettings
{
    RunMode mode;
    ShinyLayerSettings layers[LAYER_COUNT];
    int currentLayerIndex = 1;
    ShinyLayerSettings *currentLayer()
    {
        return &layers[currentLayerIndex];
    }
};

#endif
