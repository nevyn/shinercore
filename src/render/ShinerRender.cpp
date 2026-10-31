#include "include/ShinerRender.h"
#include "Render.h"
#include <vector>

int32_t shiner_animation_count(void)
{
    return AnimationCount;
}

static const AnimationInfo *animationAt(int32_t animation)
{
    return animation >= 0 && animation < AnimationCount ? &kAnimations[animation] : NULL;
}

const char *shiner_animation_name(int32_t animation)
{
    const AnimationInfo *a = animationAt(animation);
    return a ? a->name : NULL;
}

int32_t shiner_animation_colors(int32_t animation)
{
    const AnimationInfo *a = animationAt(animation);
    return a ? a->colors : 0;
}

const char *shiner_animation_parameter(int32_t animation, int32_t parameter)
{
    const AnimationInfo *a = animationAt(animation);
    if(!a) return NULL;
    return parameter == 0 ? a->tauName : parameter == 1 ? a->phiName : NULL;
}

int32_t shiner_blend_mode_count(void)
{
    return BlendModeCount;
}

const char *shiner_blend_mode_name(int32_t blendMode)
{
    return blendMode >= 0 && blendMode < BlendModeCount ? kBlendModeNames[blendMode] : NULL;
}

static float unit(float v)
{
    return v > 0 ? (v < 1 ? v : 1) : 0; // NaN lands on 0
}

void shiner_render(const ShinerLayer *layers, int32_t layerCount, float beat,
                   ShinerRGB *pixels, int32_t pixelCount)
{
    if(!pixels || pixelCount <= 0) return;
    std::vector<CRGB> frame(pixelCount, CRGB(0, 0, 0)), scratch(pixelCount);
    for(int32_t i = 0; layers && i < layerCount; i++)
    {
        const ShinerLayer &l = layers[i];
        AnimationSettings settings = {
            CRGB(l.color1.r, l.color1.g, l.color1.b), CRGB(l.color2.r, l.color2.g, l.color2.b), l.tau, l.phi,
        };
        compositeLayer(frame.data(), scratch.data(), pixelCount, l.animation, l.blendMode, settings, l.time,
                       unit(beat), unit(l.gain));
    }
    for(int32_t i = 0; i < pixelCount; i++)
    {
        pixels[i].r = frame[i].r;
        pixels[i].g = frame[i].g;
        pixels[i].b = frame[i].b;
    }
}
