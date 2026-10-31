#include "Render.h"
#include <algorithm>
#include <stdlib.h>

const char *const kBlendModeNames[BlendModeCount] = {
    "Add",
    "Subtract",
    "Add Wrap",
    "Subtract Wrap",
    "Multiply",
    "Dissolve",
    "Average",
    "Set",
    "Screen",
    "Lighten",
    "Darken",
    "Difference",
    "Overlay",
    "Color Dodge",
};

static CRGB blendPixel(const CRGB &a, const CRGB &b, int mode)
{
    using std::max;
    using std::min;
    switch(mode) {
        case BlendModeAdd: default: return a + b;
        case BlendModeSubtract: return a - b;
        case BlendModeAddWrap: return CRGB((a.r + b.r) & 0xFF, (a.g + b.g) & 0xFF, (a.b + b.b) & 0xFF);
        case BlendModeSubtractWrap: return CRGB((a.r - b.r) & 0xFF, (a.g - b.g) & 0xFF, (a.b - b.b) & 0xFF);
        case BlendModeMultiply: return CRGB((a.r * b.r) >> 8, (a.g * b.g) >> 8, (a.b * b.b) >> 8);
        case BlendModeDissolve: return random8() < 128 ? a : b;
        case BlendModeAverage: return CRGB((a.r + b.r) >> 1, (a.g + b.g) >> 1, (a.b + b.b) >> 1);
        case BlendModeSet: return b;
        case BlendModeScreen: return CRGB(255 - (((255-a.r) * (255-b.r)) >> 8),
                                          255 - (((255-a.g) * (255-b.g)) >> 8),
                                          255 - (((255-a.b) * (255-b.b)) >> 8));
        case BlendModeLighten: return CRGB(max(a.r, b.r), max(a.g, b.g), max(a.b, b.b));
        case BlendModeDarken: return CRGB(min(a.r, b.r), min(a.g, b.g), min(a.b, b.b));
        case BlendModeDifference: return CRGB(abs(a.r - b.r), abs(a.g - b.g), abs(a.b - b.b));
        case BlendModeOverlay: return CRGB(
            a.r < 128 ? (2 * a.r * b.r) >> 8 : 255 - ((2 * (255-a.r) * (255-b.r)) >> 8),
            a.g < 128 ? (2 * a.g * b.g) >> 8 : 255 - ((2 * (255-a.g) * (255-b.g)) >> 8),
            a.b < 128 ? (2 * a.b * b.b) >> 8 : 255 - ((2 * (255-a.b) * (255-b.b)) >> 8));
        case BlendModeColorDodge: return CRGB(
            b.r == 255 ? 255 : min(255, (a.r << 8) / (255 - b.r)),
            b.g == 255 ? 255 : min(255, (a.g << 8) / (255 - b.g)),
            b.b == 255 ? 255 : min(255, (a.b << 8) / (255 - b.b)));
    }
}

void compositeLayer(CRGB *frame, CRGB *scratch, int count, int animation, int blendMode,
                    const AnimationSettings &settings, double t, float beat, float gain)
{
    if(animation == 0 || gain == 0) return; // Nothing, or gated off: skip the fill and blend entirely
    uint8_t amount = gain * 255 + 0.5f;

    for(int i = 0; i < count; i++) scratch[i] = CRGB::Black;
    if(animation > 0 && animation < AnimationCount) kAnimations[animation].render(scratch, count, settings, t, beat);

    for(int i = 0; i < count; i++)
    {
        CRGB blended = blendPixel(frame[i], scratch[i], blendMode);
        frame[i] = amount == 255 ? blended : blend(frame[i], blended, amount);
    }
}
