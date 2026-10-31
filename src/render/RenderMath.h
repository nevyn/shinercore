// Render-private math. The names differ from OverAnimate's gammaf and
// operator*(CRGB, float), so both can share a translation unit; the results match
// them bit for bit.
#ifndef SHINER_RENDER_RENDERMATH_H
#define SHINER_RENDER_RENDERMATH_H
#include "FastLED.h"
#include <math.h>
#include <stdint.h>

namespace render {

// Over the input range 0.0f-1.0f, returns one full sine cycle
// mapped to [0, 1], starting and ending at 0.0f, with a peak value of 1.0f.
inline float curve(float progress)
{
    return sin((progress-0.25)*6.28f)/2.0f + 0.5f;
}

// Deterministic pseudo-random from position/time, which keeps "random" effects stateless
inline uint32_t hash(uint32_t x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

// Pseudo-random float 0-1 for a given seed
inline float hashFloat(uint32_t seed)
{
    return (hash(seed) & 0xFFFF) / 65535.0f;
}

// tau/phi are divisors in the wave animations, can be set to 0, and ease
// through 0 when changing sign; keep the division finite
inline float nonzero(float v)
{
    return fabsf(v) < 0.05f ? (v < 0 ? -0.05f : 0.05f) : v;
}

static const uint8_t kGamma8[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255
};

// Gamma-corrects a 0-1 brightness
inline float gammaCorrect(float f)
{
    return kGamma8[(uint8_t)(f*255)]/(float)255;
}

// C++ leaves out-of-range double-to-integer conversions undefined; elsewhere these
// return what the ESP32's libgcc does (__fixdfsi, __fixunsdfsi), so hosts draw what the strip shows.
inline int32_t toInt(double x)
{
#ifdef __XTENSA__
    return (int32_t)x;
#else
    if(x != x || x >= 2147483648.0) return INT32_MAX;
    if(x <= -2147483648.0) return INT32_MIN;
    return (int32_t)x;
#endif
}

// Wraps modulo 256, like a signed conversion
inline uint8_t toByte(double x)
{
#ifdef __XTENSA__
    return (uint8_t)x;
#else
    if(x != x || x >= 4294967296.0) return 0xFF;
    if(x <= -2147483648.0) return 0;
    return (uint8_t)(int64_t)x;
#endif
}

// Scales a color by 0-1, truncating each channel
inline CRGB scale(const CRGB &c, float f)
{
    return CRGB(c.r*f, c.g*f, c.b*f);
}

}

#endif
