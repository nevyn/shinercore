#ifndef __CODECS__H
#define __CODECS__H
#include "ShinyTypes.h"
#include <stdlib.h>
#include <math.h>

// Parses and serializes property values at the NVS/BLE boundary. decode returns
// false on garbage, so a bad write can be rejected instead of zeroing state.
template<typename T> struct Codec;

template<> struct Codec<int> {
    static bool decode(const String &s, int &out)
    {
        char *end; long v = strtol(s.c_str(), &end, 10);
        if(end == s.c_str() || *end != '\0') return false;
        out = v; return true;
    }
    static String encode(int v) { return String(v); }
};

template<> struct Codec<float> {
    static bool decode(const String &s, float &out)
    {
        char *end; float v = strtof(s.c_str(), &end);
        if(end == s.c_str() || *end != '\0' || !isfinite(v)) return false;
        out = v; return true;
    }
    static String encode(float v) { return String(v, 3); }
};

template<> struct Codec<String> {
    static bool decode(const String &s, String &out) { out = s; return true; }
    static String encode(const String &v) { return v; }
};

template<> struct Codec<CRGB> {
    static bool decode(const String &s, CRGB &out)
    {
        long c[3]; const char *p = s.c_str(); char *end;
        for(int i = 0; i < 3; i++)
        {
            c[i] = strtol(p, &end, 10);
            if(end == p || c[i] < 0 || c[i] > 255) return false;
            p = end;
        }
        if(*p != '\0') return false;
        out = CRGB(c[0], c[1], c[2]); return true;
    }
    static String encode(const CRGB &v) { return String(v.r) + " " + String(v.g) + " " + String(v.b); }
};

template<> struct Codec<RunMode> {
    static bool decode(const String &s, RunMode &out)
    {
        int v;
        if(!Codec<int>::decode(s, v) || v < 0 || v >= RunModeCount) return false;
        out = (RunMode)v; return true;
    }
    static String encode(RunMode v) { return String((int)v); }
};

// Enums travel by name, with a numeric-index fallback for older app versions
static inline bool decodeNamed(const std::vector<String> &names, const String &s, int &out)
{
    for(size_t i = 0; i < names.size(); i++)
    {
        if(s == names[i]) { out = i; return true; }
    }
    int v;
    if(Codec<int>::decode(s, v) && v >= 0 && v < (int)names.size()) { out = v; return true; }
    return false;
}

template<> struct Codec<LayerBlendMode> {
    static bool decode(const String &s, LayerBlendMode &out)
    {
        int i;
        if(!decodeNamed(blendModeNames, s, i)) return false;
        out = (LayerBlendMode)i; return true;
    }
    static String encode(LayerBlendMode v) { return blendModeNames[v]; }
};

template<> struct Codec<LedColorOrder> {
    static bool decode(const String &s, LedColorOrder &out)
    {
        int i;
        if(!decodeNamed(ledColorOrderNames, s, i)) return false;
        out = (LedColorOrder)i; return true;
    }
    static String encode(LedColorOrder v) { return ledColorOrderNames[v]; }
};

// The animation is an index in ShinyLayerSettings, but names on the wire
extern std::vector<String> animationNames;
struct AnimationCodec {
    static bool decode(const String &s, int &out) { return decodeNamed(animationNames, s, out); }
    static String encode(int v) { return animationNames[v]; }
};

// Range clamping for the types where a range makes sense; identity for the rest
// (enums are validated by their codec, CRGB components can't leave 0-255).
static inline int clampTo(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float clampTo(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline RunMode clampTo(RunMode v, RunMode lo, RunMode hi) { return (RunMode)clampTo((int)v, (int)lo, (int)hi); }
static inline String clampTo(const String &v, const String&, const String&) { return v; }
static inline CRGB clampTo(const CRGB &v, const CRGB&, const CRGB&) { return v; }
static inline LayerBlendMode clampTo(LayerBlendMode v, LayerBlendMode, LayerBlendMode) { return v; }
static inline LedColorOrder clampTo(LedColorOrder v, LedColorOrder, LedColorOrder) { return v; }

#endif
