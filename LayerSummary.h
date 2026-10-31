#ifndef __LAYER_SUMMARY__H
#define __LAYER_SUMMARY__H
#include <math.h>
#include <stdlib.h>
#include "ShinyTypes.h"

// The pure half of the layer summary, layer opacities and the layer move (docs/ble-comms.md,
// The layer summary): text formats and array shuffling, no BLE. Host-tested in test/layertest.cpp.

// formatLayerSummary's preset while the mesh carousel plays a neighbor's preset
static const int kLayerSummaryMesh = -1;
// The ATT maximum attribute length
static const int kLayerSummaryMaxBytes = 512;
// The longest `layerOpacities` value: ten "0.125"s and nine spaces fit
static const int kLayerOpacitiesMaxBytes = 64;

// A NUL-terminated text of at most kLayerSummaryMaxBytes; appends past that are dropped.
struct LayerSummaryText
{
    char chars[kLayerSummaryMaxBytes + 1] = {0};
    int length = 0;

    void put(char c)
    {
        if(length == kLayerSummaryMaxBytes) return;
        chars[length++] = c;
        chars[length] = '\0';
    }
    void putInt(long v)
    {
        if(v < 0) { put('-'); v = -v; }
        char digits[12];
        int n = 0;
        do { digits[n++] = '0' + v % 10; v /= 10; } while(v);
        while(n) put(digits[--n]);
    }
    // Six lowercase hex digits, rrggbb
    void putColor(const CRGB &c)
    {
        static const char hex[] = "0123456789abcdef";
        const uint8_t channels[3] = {c.r, c.g, c.b};
        for(int i = 0; i < 3; i++) { put(hex[channels[i] >> 4]); put(hex[channels[i] & 15]); }
    }
    // At most 3 decimals, trailing zeros and a trailing point dropped: 2.5, 10, -0.125
    void putFloat(float v)
    {
        long thousandths = lroundf(v * 1000);
        if(thousandths < 0) { put('-'); thousandths = -thousandths; }
        putInt(thousandths / 1000);
        int fraction = thousandths % 1000;
        if(!fraction) return;
        put('.');
        put('0' + fraction / 100);
        if(fraction % 100) put('0' + fraction / 10 % 10);
        if(fraction % 10) put('0' + fraction % 10);
    }
};

// Formats the `layers` value into out: the header (`P preset`, or `M` for
// kLayerSummaryMesh), then one line per layer slot whose animation isn't Nothing,
// bottom first. layers are the settings each slot plays; gates are the layer gates.
static inline void formatLayerSummary(LayerSummaryText &out, int preset,
                                      const ShinyLayerSettings *const layers[LAYER_COUNT],
                                      const bool gates[LAYER_COUNT])
{
    out.length = 0;
    out.chars[0] = '\0';
    if(preset == kLayerSummaryMesh) out.put('M');
    else { out.put('P'); out.put(' '); out.putInt(preset); }
    for(int i = 0; i < LAYER_COUNT; i++)
    {
        const ShinyLayerSettings &l = *layers[i];
        if(l.animationIndex == 0) continue; // Nothing
        out.put('\n');
        out.putInt(i); out.put(' ');
        out.putInt(l.animationIndex); out.put(' ');
        out.putInt(l.blendMode); out.put(' ');
        out.putColor(l.mainColor); out.put(' ');
        out.putColor(l.secondaryColor); out.put(' ');
        out.putFloat(l.speed); out.put(' ');
        out.putFloat(l.p_tau); out.put(' ');
        out.putFloat(l.p_phi); out.put(' ');
        out.put(l.beatSync ? '1' : '0'); out.put(' ');
        out.put(gates[i] ? '1' : '0');
    }
}

// Formats the `layerOpacities` value into out: the opacity of every layer slot, slot 0
// first, separated by single spaces, in the `layers` float format. layers are the
// settings each slot plays.
static inline void formatLayerOpacities(LayerSummaryText &out, const ShinyLayerSettings *const layers[LAYER_COUNT])
{
    out.length = 0;
    out.chars[0] = '\0';
    for(int i = 0; i < LAYER_COUNT; i++)
    {
        if(i) out.put(' ');
        out.putFloat(layers[i]->opacity);
    }
}

// Parses one layer index at p and advances p past it.
static inline bool decodeLayerIndex(const char *&p, int &out)
{
    if(*p < '0' || *p > '9') return false;
    char *end;
    long v = strtol(p, &end, 10);
    p = end;
    if(v >= LAYER_COUNT) return false;
    out = v;
    return true;
}

// Parses a `layerMove` write, `FROM TO`. False unless it is exactly two layer
// indices separated by one space.
static inline bool decodeLayerMove(const char *s, int &from, int &to)
{
    return decodeLayerIndex(s, from) && *s++ == ' ' && decodeLayerIndex(s, to) && *s == '\0';
}

// Moves layers[from] to index `to`, shifting the layers in between: remove, then
// insert. from and to must be layer indices.
static inline void moveLayer(ShinyLayerSettings layers[LAYER_COUNT], int from, int to)
{
    ShinyLayerSettings moving = layers[from];
    for(int i = from; i < to; i++) layers[i] = layers[i + 1];
    for(int i = from; i > to; i--) layers[i] = layers[i - 1];
    layers[to] = moving;
}

#endif
