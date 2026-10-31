// Host tests for LayerSummary.h: the `layers` and `layerOpacities` texts, float
// trimming, layerMove parsing and the move itself.
#include "FastLED.h"
#include "../LayerSummary.h"
#include <chrono>
#include <cstring>

static int failures = 0;
static void expect(const char *what, const char *got, const char *want) {
    bool ok = strcmp(got, want) == 0;
    if(!ok) failures++;
    printf("  %s %s = '%s' (want '%s')\n", ok ? "ok  " : "FAIL", what, got, want);
}
static void check(const char *what, bool ok) {
    if(!ok) failures++;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

static const char *floatText(float v) {
    static LayerSummaryText t;
    t = LayerSummaryText();
    t.putFloat(v);
    return t.chars;
}

struct Stack {
    ShinyLayerSettings layers[LAYER_COUNT];
    const ShinyLayerSettings *slots[LAYER_COUNT];
    bool gates[LAYER_COUNT];
    Stack() {
        for(int i = 0; i < LAYER_COUNT; i++) { slots[i] = &layers[i]; gates[i] = true; }
    }
    const char *summary(int preset) {
        static LayerSummaryText t;
        formatLayerSummary(t, preset, slots, gates);
        return t.chars;
    }
    const char *opacities() {
        static LayerSummaryText t;
        formatLayerOpacities(t, slots);
        return t.chars;
    }
};

// Tags a layer by its tau, so moves are easy to read back
static void tagLayers(ShinyLayerSettings layers[LAYER_COUNT]) {
    for(int i = 0; i < LAYER_COUNT; i++) { layers[i] = ShinyLayerSettings(); layers[i].p_tau = i; }
}
static const char *order(const ShinyLayerSettings layers[LAYER_COUNT]) {
    static char s[LAYER_COUNT + 1];
    for(int i = 0; i < LAYER_COUNT; i++) s[i] = '0' + (int)layers[i].p_tau;
    s[LAYER_COUNT] = '\0';
    return s;
}

int main() {
    printf("float trimming\n");
    expect("2.5", floatText(2.5f), "2.5");
    expect("10", floatText(10.0f), "10");
    expect("-0.125", floatText(-0.125f), "-0.125");
    expect("0.001", floatText(0.001f), "0.001");
    expect("0.05", floatText(0.05f), "0.05");
    expect("0.1 despite binary error", floatText(0.1f), "0.1");
    expect("100", floatText(100.0f), "100");
    expect("-100", floatText(-100.0f), "-100");
    expect("0", floatText(0.0f), "0");
    expect("rounds to 3 decimals", floatText(1.23456f), "1.235");
    expect("no negative zero", floatText(-0.0004f), "0");
    expect("-99.999", floatText(-99.999f), "-99.999");

    printf("summary\n");
    {
        Stack s;
        expect("empty preset is just the header", s.summary(0), "P 0");
        expect("mesh header", s.summary(kLayerSummaryMesh), "M");

        // The contract's example: layer 0, animation 11, Add, default colors, speed 6
        ShinyLayerSettings &l = s.layers[0];
        l.animationIndex = 11;
        l.speed = 6;
        l.p_tau = 6;
        expect("contract example", s.summary(1), "P 1\n0 11 0 ff6400 f0ff00 6 6 4 0 1");

        ShinyLayerSettings &top = s.layers[7];
        top.animationIndex = 3;
        top.blendMode = BlendModeMultiply;
        top.mainColor = CRGB(0x0a, 0xbc, 0xde);
        top.secondaryColor = CRGB(0, 0, 0);
        top.speed = 0.25f;
        top.p_tau = -12.5f;
        top.p_phi = 0.001f;
        top.beatSync = 1;
        s.gates[7] = false;
        expect("bottom first, Nothing skipped, closed gate",
               s.summary(kLayerSummaryMesh),
               "M\n0 11 0 ff6400 f0ff00 6 6 4 0 1\n7 3 4 0abcde 000000 0.25 -12.5 0.001 1 0");

        // Settings the slots point at, not the array they live in: the carousel's targets
        ShinyLayerSettings neighbor;
        neighbor.animationIndex = 2;
        s.slots[0] = &neighbor;
        s.layers[7].animationIndex = 0;
        expect("reads through the slot pointers", s.summary(kLayerSummaryMesh), "M\n0 2 0 ff6400 f0ff00 1 10 4 0 1");
    }

    printf("worst case fits one characteristic\n");
    {
        Stack s;
        for(int i = 0; i < LAYER_COUNT; i++) {
            ShinyLayerSettings &l = s.layers[i];
            l.animationIndex = AnimationCount - 1;
            l.blendMode = (LayerBlendMode)(BlendModeCount - 1);
            l.speed = 99.999f;
            l.p_tau = -99.999f;
            l.p_phi = -99.999f;
            l.beatSync = 1;
        }
        const char *text = s.summary(PRESET_COUNT - 1);
        int length = strlen(text);
        printf("  worst case is %d bytes\n", length);
        check("under 512 bytes", length < kLayerSummaryMaxBytes);
        check("ten lines after the header", strstr(text, "\n9 13 13 ff6400 f0ff00 99.999 -99.999 -99.999 1 1") != nullptr);

        // Host time per summary, to scale against the ESP32's frame budget
        LayerSummaryText t;
        const int runs = 100000;
        auto start = std::chrono::steady_clock::now();
        for(int i = 0; i < runs; i++) formatLayerSummary(t, PRESET_COUNT - 1, s.slots, s.gates);
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / runs;
        check("timed runs format the same text", strcmp(t.chars, text) == 0);
        printf("  worst case formats in %.2f us on this host\n", us);
    }

    printf("opacities\n");
    {
        Stack s;
        expect("defaults", s.opacities(), "1 1 1 1 1 1 1 1 1 1");
        s.layers[1].opacity = 0.3f;
        s.layers[9].opacity = 0.125f;
        s.layers[4].opacity = 0;
        expect("fractions, Nothing slots included", s.opacities(), "1 0.3 1 1 0 1 1 1 1 0.125");
        ShinyLayerSettings neighbor;
        neighbor.opacity = 0.5f;
        s.slots[0] = &neighbor;
        expect("reads through the slot pointers", s.opacities(), "0.5 0.3 1 1 0 1 1 1 1 0.125");
        for(int i = 0; i < LAYER_COUNT; i++) s.layers[i].opacity = 0.999f;
        s.slots[0] = &s.layers[0];
        int length = strlen(s.opacities());
        printf("  worst case is %d bytes\n", length);
        check("worst case fits its characteristic", length <= kLayerOpacitiesMaxBytes);
    }

    printf("overflow drops, never writes past the buffer\n");
    {
        LayerSummaryText t;
        for(int i = 0; i < kLayerSummaryMaxBytes + 10; i++) t.put('x');
        check("length capped", t.length == kLayerSummaryMaxBytes);
        check("still terminated", t.chars[kLayerSummaryMaxBytes] == '\0');
    }

    printf("layerMove parsing\n");
    {
        int from = -1, to = -1;
        check("3 7", decodeLayerMove("3 7", from, to) && from == 3 && to == 7);
        check("0 9", decodeLayerMove("0 9", from, to) && from == 0 && to == 9);
        check("same index", decodeLayerMove("4 4", from, to) && from == 4 && to == 4);
        check("reject 10", !decodeLayerMove("10 2", from, to));
        check("reject TO 10", !decodeLayerMove("2 10", from, to));
        check("reject negative", !decodeLayerMove("-1 2", from, to));
        check("reject plus sign", !decodeLayerMove("+1 2", from, to));
        check("reject one index", !decodeLayerMove("3", from, to));
        check("reject trailing space", !decodeLayerMove("3 4 ", from, to));
        check("reject leading space", !decodeLayerMove(" 3 4", from, to));
        check("reject two spaces", !decodeLayerMove("3  4", from, to));
        check("reject three indices", !decodeLayerMove("1 2 3", from, to));
        check("reject empty", !decodeLayerMove("", from, to));
        check("reject garbage", !decodeLayerMove("up down", from, to));
        check("reject huge", !decodeLayerMove("99999999999999999999 1", from, to));
    }

    printf("move\n");
    {
        ShinyLayerSettings l[LAYER_COUNT];
        tagLayers(l); moveLayer(l, 2, 5); expect("forward", order(l), "0134526789");
        tagLayers(l); moveLayer(l, 5, 2); expect("backward", order(l), "0152346789");
        tagLayers(l); moveLayer(l, 4, 4); expect("same index", order(l), "0123456789");
        tagLayers(l); moveLayer(l, 0, 9); expect("bottom to top", order(l), "1234567890");
        tagLayers(l); moveLayer(l, 9, 0); expect("top to bottom", order(l), "9012345678");
        tagLayers(l); moveLayer(l, 0, 1); expect("swap at the bottom", order(l), "1023456789");
        tagLayers(l); moveLayer(l, 9, 8); expect("swap at the top", order(l), "0123456798");
        tagLayers(l); moveLayer(l, 0, 0); expect("bottom in place", order(l), "0123456789");
        tagLayers(l); moveLayer(l, 9, 9); expect("top in place", order(l), "0123456789");

        tagLayers(l);
        l[3].animationIndex = 5; l[3].mainColor = CRGB(1, 2, 3); l[3].beatSync = 1;
        moveLayer(l, 3, 7);
        check("every field travels", l[7].animationIndex == 5 && l[7].mainColor == CRGB(1, 2, 3) && l[7].beatSync == 1 && l[7].p_tau == 3);
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
