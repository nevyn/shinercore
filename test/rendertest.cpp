// Host tests for the render library (src/render/): the animation table, and frames
// pinned to what the firmware drew before the library existed.
#include "FastLED.h"
#include "../ShinyTypes.h"
#include "../src/render/include/ShinerRender.h"
#include <string.h>

struct GoldenFrame {
    const char *name;
    int layerCount;
    ShinerLayer layers[2];
    float beat;
    const char *pixels;
};
#include "rendergoldens.h"

static int failures = 0;
static void check(const char *what, bool ok) {
    if(!ok) failures++;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

static bool same(const char *a, const char *b) { return a == b || (a && b && strcmp(a, b) == 0); }

static std::string hex(const CRGB *pixels, int count)
{
    std::string s;
    char buf[8];
    for(int i = 0; i < count; i++) { snprintf(buf, sizeof buf, "%02x%02x%02x", pixels[i].r, pixels[i].g, pixels[i].b); s += buf; }
    return s;
}

static CRGB rgb(const ShinerRGB &c) { return CRGB(c.r, c.g, c.b); }

// The firmware's path: one compositeLayer per layer onto a black frame
static std::string composite(const GoldenFrame &g)
{
    CRGB frame[kGoldenPixels], scratch[kGoldenPixels];
    random16_set_seed(1337);
    for(int i = 0; i < g.layerCount; i++)
    {
        const ShinerLayer &l = g.layers[i];
        AnimationSettings settings = {rgb(l.color1), rgb(l.color2), l.tau, l.phi};
        compositeLayer(frame, scratch, kGoldenPixels, l.animation, l.blendMode, settings, l.time, g.beat, l.gain);
    }
    return hex(frame, kGoldenPixels);
}

static void printGoldens()
{
    printf("static const int kGoldenPixels = %d;\n", kGoldenPixels);
    printf("static const GoldenFrame kGoldenFrames[] = {\n");
    for(const GoldenFrame &g : kGoldenFrames)
    {
        printf("    {\"%s\", %d, {", g.name, g.layerCount);
        for(int i = 0; i < g.layerCount; i++)
        {
            const ShinerLayer &l = g.layers[i];
            printf("%s{%d, %d, {%d, %d, %d}, {%d, %d, %d}, %.9g, %.9g, %.17g, %.9g}", i ? ", " : "", l.animation, l.blendMode,
                   l.color1.r, l.color1.g, l.color1.b, l.color2.r, l.color2.g, l.color2.b, l.tau, l.phi, l.time, l.gain);
        }
        printf("}, %.9g,\n     \"%s\"},\n", g.beat, composite(g).c_str());
    }
    printf("};\n");
}

int main(int argc, char **argv)
{
    if(argc > 1 && strcmp(argv[1], "--print-goldens") == 0) { printGoldens(); return 0; }

    printf("animation table\n");
    {
        // Indices are persisted in NVS and sent over the mesh
        const char *names[] = {"Nothing", "Static", "Opposing Waves", "Single Wave", "Breathe", "Rainbow", "Comet",
                               "Scanner", "Twinkle", "Theater Chase", "Color Wipe", "Gradient Pulse", "Sparkle", "Beat Pulse"};
        struct { int colors; const char *tau, *phi; } docs[] = {
            {0, NULL, NULL}, {1, NULL, NULL}, {2, "Wave length", "Counter-wave length"}, {1, "Wave length", "Phase"},
            {2, NULL, NULL}, {0, "Rainbows", "Drift speed"}, {2, "Tail length", "Head width"}, {2, "Width", "Glow"},
            {2, "Density", "Twinkle speed"}, {2, "Spacing", "Group size"}, {2, "Wipe time", NULL},
            {2, "Sharpness", "Repeats"}, {2, "Flash length", "Density"}, {1, NULL, NULL},
        };
        check("count", AnimationCount == 14);
        for(int i = 0; i < AnimationCount; i++)
        {
            const AnimationInfo &a = kAnimations[i];
            std::string what = std::to_string(i) + " " + names[i];
            check((what + " name").c_str(), same(a.name, names[i]));
            check((what + " renders").c_str(), a.render != NULL);
            check((what + " colors").c_str(), a.colors >= 0 && a.colors <= 2 && a.colors == docs[i].colors);
            check((what + " parameter names").c_str(), same(a.tauName, docs[i].tau) && same(a.phiName, docs[i].phi));
        }
        bool derived = animationNames.size() == AnimationCount;
        for(int i = 0; derived && i < AnimationCount; i++) derived = animationNames[i] == kAnimations[i].name;
        check("animationNames follows the table", derived);
    }

    printf("blend modes\n");
    {
        const char *names[] = {"Add", "Subtract", "Add Wrap", "Subtract Wrap", "Multiply", "Dissolve", "Average",
                               "Set", "Screen", "Lighten", "Darken", "Difference", "Overlay", "Color Dodge"};
        bool ok = BlendModeCount == 14 && blendModeNames.size() == BlendModeCount;
        for(int i = 0; ok && i < BlendModeCount; i++) ok = same(kBlendModeNames[i], names[i]) && blendModeNames[i] == names[i];
        check("names in wire order", ok);
    }

    printf("golden frames (firmware path)\n");
    {
        int matched = 0, total = sizeof kGoldenFrames / sizeof *kGoldenFrames;
        for(const GoldenFrame &g : kGoldenFrames)
        {
            std::string got = composite(g);
            if(got == g.pixels) { matched++; continue; }
            printf("  FAIL %s\n    want %s\n    got  %s\n", g.name, g.pixels, got.c_str());
            failures++;
        }
        printf("  %s %d of %d frames match\n", matched == total ? "ok  " : "FAIL", matched, total);
    }

    printf("device number semantics\n");
    {
        // xtensa's __fixunsdfsi wraps a negative double the way a signed conversion does
        CRGB frame[4] = {}, scratch[4];
        AnimationSettings rainbow = {CRGB(), CRGB(), 10, -13.25f};
        compositeLayer(frame, scratch, 4, 5, BlendModeAdd, rainbow, 1.0, 0, 1);
        check("Rainbow drifting backwards wraps the hue", frame[0] == CRGB(CHSV((uint8_t)(int32_t)(-13.25 / 4 * 256), 240, 255)));
    }

    printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
