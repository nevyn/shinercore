#ifndef __TRANSITION__H
#define __TRANSITION__H
#include <math.h>
#include <stdlib.h>
#include "ShinyTypes.h"

// Settings changes reach the pixels by easing when small and by crossfading
// when large; see docs/transitions.md. The thresholds bound how far an ease
// travels, which is what bounds how long it takes.
static const float kEaseSeconds = 0.1f;      // time constant of the exponential ease
static const float kCrossfadeSeconds = 0.5f;
static const int kEaseMaxColorStep = 48;     // per channel; larger jumps crossfade
static const float kEaseMaxRatio = 1.5f;     // speed, tau and phi; larger ratios crossfade
static const float kEaseLandRatio = 0.01f;   // a float this close (relative) lands on its target

// How far to move toward a target this frame
static inline float easeAlpha(float dt) { return 1 - expf(-dt / kEaseSeconds); }

// Exponential approach that lands: within `snap` of the target is the target.
static inline float easeTo(float current, float target, float alpha, float snap)
{
    float v = current + (target - current) * alpha;
    return fabsf(target - v) < snap ? target : v;
}

static inline float easeParam(float current, float target, float alpha)
{
    return easeTo(current, target, alpha, kEaseLandRatio * fmaxf(fabsf(target), 1.0f));
}

// Eases in log space, so a doubling of speed takes as long as a halving.
static inline float easeSpeed(float current, float target, float alpha)
{
    if(current <= 0 || target <= 0) return target;
    float v = current * powf(target / current, alpha);
    return fabsf(logf(target / v)) < kEaseLandRatio ? target : v;
}

static inline CRGB easeColor(CRGB current, CRGB target, float alpha)
{
    // round away from current so the last step actually lands on the target
    CRGB out;
    for(int ch = 0; ch < 3; ch++)
    {
        float v = current[ch] + (target[ch] - current[ch]) * alpha;
        out[ch] = target[ch] > current[ch] ? (uint8_t)ceilf(v) : (uint8_t)floorf(v);
    }
    return out;
}

static inline bool farColor(CRGB a, CRGB b)
{
    for(int ch = 0; ch < 3; ch++)
    {
        if(abs(a[ch] - b[ch]) > kEaseMaxColorStep) return true;
    }
    return false;
}

// tau and phi divide the pixel index, so a sign flip or a move off zero reshapes the whole strip
static inline bool farRatio(float a, float b)
{
    if(a == b) return false;
    if(a == 0 || b == 0 || (a < 0) != (b < 0)) return true;
    float r = a / b;
    return r > kEaseMaxRatio || r < 1 / kEaseMaxRatio;
}

// True when easing from shown to target would show intermediate states that
// neither looks like, or would take longer than kCrossfadeSeconds.
static inline bool needsCrossfade(const ShinyLayerSettings &shown, const ShinyLayerSettings &target)
{
    return shown.animationIndex != target.animationIndex
        || shown.blendMode != target.blendMode
        || farColor(shown.mainColor, target.mainColor)
        || farColor(shown.secondaryColor, target.secondaryColor)
        || farRatio(shown.speed, target.speed)
        || farRatio(shown.p_tau, target.p_tau)
        || farRatio(shown.p_phi, target.p_phi);
}

#endif
