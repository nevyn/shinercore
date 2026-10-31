// The render library's C++ side: the animation table, the blend modes and the
// per-layer composite step that every frame is built from. The firmware uses it
// directly; the Remote app goes through the C API in include/ShinerRender.h.
// See docs/rendering.md.
#ifndef SHINER_RENDER_RENDER_H
#define SHINER_RENDER_RENDER_H
#include "FastLED.h"

// Indices are persisted in NVS and sent over the mesh: append only.
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
/// Each blend mode's name, as the `documentation` characteristic lists it.
extern const char *const kBlendModeNames[BlendModeCount];

/// What an animation reads of a layer's settings.
struct AnimationSettings
{
    CRGB mainColor, secondaryColor;
    float p_tau, p_phi;
};

/// Draws one frame of an animation into `pixels`, which arrive black. `t` is cycles
/// elapsed (unbounded); `beat` is the beat envelope 0-1. Must be a pure function of
/// its arguments: no state kept between frames.
typedef void (*AnimationFunc)(CRGB *pixels, int count, const AnimationSettings &prefs, double t, float beat);

struct AnimationInfo
{
    /// The name on the wire and in NVS.
    const char *name;
    AnimationFunc render;
    /// How many of mainColor and secondaryColor the animation reads: 0, 1 or 2.
    int colors;
    /// The user-facing names of tau and phi; NULL where the animation ignores one.
    const char *tauName;
    const char *phiName;
};

// Indices are persisted in NVS and sent over the mesh: append only.
enum { AnimationCount = 14 };
/// Every animation, in index order. Index 0 is "Nothing", which renders no layer at all.
extern const AnimationInfo kAnimations[];

/// One layer's step of a frame: renders animation `animation` into `scratch`, then
/// blends that into `frame` with `blendMode` (a LayerBlendMode), mixed in at `gain`
/// (0-1). Both buffers hold `count` pixels. Does nothing for animation 0 (Nothing) or
/// gain 0; any other out-of-range animation renders black, which still blends. An
/// out-of-range blend mode blends as Add.
void compositeLayer(CRGB *frame, CRGB *scratch, int count, int animation, int blendMode,
                    const AnimationSettings &settings, double t, float beat, float gain);

#endif
