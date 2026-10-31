// ShinerRender: the firmware's animations and blend modes as a C API, so that the
// firmware and the Remote app draw a strip with the same code. See docs/rendering.md.
#ifndef SHINER_RENDER_H
#define SHINER_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r, g, b;
} ShinerRGB;

/// One layer as the renderer sees it: a layer's settings plus its playback state.
typedef struct {
    /// Index into the animation table (shiner_animation_name). Out of range renders black.
    int32_t animation;
    /// Index into the blend mode table (shiner_blend_mode_name). Out of range blends as Add.
    int32_t blendMode;
    /// The layer's primary and secondary colors (`color` and `color2` on the wire).
    ShinerRGB color1, color2;
    /// The animation's two parameters; shiner_animation_parameter names them.
    float tau, phi;
    /// Animation cycles elapsed: seconds divided by the layer's speed, or beats divided
    /// by speed when the layer is beat synced. Unbounded; only the fraction is periodic.
    double time;
    /// How strongly the layer mixes in, 0-1. 0 skips the layer, 1 blends it at full
    /// strength. The firmware passes the layer gate's fade times the layer's opacity.
    float gain;
} ShinerLayer;

/// The number of animations. Indices 0..<count are valid; 0 is "Nothing".
int32_t shiner_animation_count(void);
/// The animation's name, as the `documentation` characteristic lists it. NULL when out of range.
const char *shiner_animation_name(int32_t animation);
/// How many of the layer's colors the animation reads: 0 (none), 1 (color1 only)
/// or 2 (both). 0 when out of range.
int32_t shiner_animation_colors(int32_t animation);
/// The user-facing name of parameter 0 (tau) or 1 (phi), such as "Tail length".
/// NULL when the animation ignores that parameter, or when either index is out of range.
const char *shiner_animation_parameter(int32_t animation, int32_t parameter);

/// The number of blend modes. Indices 0..<count are valid; 0 is "Add".
int32_t shiner_blend_mode_count(void);
/// The blend mode's name, as the `documentation` characteristic lists it. NULL when out of range.
const char *shiner_blend_mode_name(int32_t blendMode);

/// Composites `layers` bottom-up (index 0 first) onto black and writes the result to
/// `pixels`, the way one firmware frame does, before global brightness and the power cap.
/// `beat` is the beat envelope 0-1 that Beat Pulse reads; pass 0 without a beat source.
/// Dissolve picks pixels at random, so its output differs between calls.
void shiner_render(const ShinerLayer *layers, int32_t layerCount, float beat,
                   ShinerRGB *pixels, int32_t pixelCount);

#ifdef __cplusplus
}
#endif

#endif
