// Host tests for the render library (src/render/): the animation table, and frames
// pinned to what the firmware drew before the library existed.
#include "FastLED.h"
#include "../ShinyTypes.h"
#include "../src/render/include/ShinerRender.h"
#include <math.h>
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

    printf("C API\n");
    {
        check("counts", shiner_animation_count() == AnimationCount && shiner_blend_mode_count() == BlendModeCount);
        check("names", same(shiner_animation_name(6), "Comet") && same(shiner_blend_mode_name(13), "Color Dodge"));
        check("out-of-range names are NULL", !shiner_animation_name(-1) && !shiner_animation_name(14) &&
              !shiner_blend_mode_name(-1) && !shiner_blend_mode_name(14));
        check("colors", shiner_animation_colors(5) == 0 && shiner_animation_colors(13) == 1 && shiner_animation_colors(6) == 2 &&
              shiner_animation_colors(-1) == 0 && shiner_animation_colors(14) == 0);
        check("parameter names", same(shiner_animation_parameter(6, 0), "Tail length") && same(shiner_animation_parameter(6, 1), "Head width"));
        check("ignored parameters are NULL", !shiner_animation_parameter(0, 0) && !shiner_animation_parameter(4, 1) &&
              !shiner_animation_parameter(10, 1) && same(shiner_animation_parameter(10, 0), "Wipe time"));
        check("out-of-range parameter names are NULL", !shiner_animation_parameter(6, 2) && !shiner_animation_parameter(6, -1) &&
              !shiner_animation_parameter(14, 0) && !shiner_animation_parameter(-1, 0));

        int matched = 0, total = sizeof kGoldenFrames / sizeof *kGoldenFrames;
        for(const GoldenFrame &g : kGoldenFrames)
        {
            ShinerRGB out[kGoldenPixels];
            random16_set_seed(1337);
            shiner_render(g.layers, g.layerCount, g.beat, out, kGoldenPixels);
            CRGB frame[kGoldenPixels];
            for(int i = 0; i < kGoldenPixels; i++) frame[i] = rgb(out[i]);
            std::string got = hex(frame, kGoldenPixels);
            if(got == g.pixels) { matched++; continue; }
            printf("  FAIL %s\n    want %s\n    got  %s\n", g.name, g.pixels, got.c_str());
            failures++;
        }
        printf("  %s %d of %d golden frames match\n", matched == total ? "ok  " : "FAIL", matched, total);

        const ShinerRGB red = {255, 0, 0}, green = {0, 255, 0};
        ShinerLayer base = {1, BlendModeAdd, red, red, 10, 4, 0, 1};
        ShinerLayer top = {1, BlendModeSet, green, green, 10, 4, 0, 1};
        auto render = [&](ShinerLayer t) {
            ShinerLayer ls[2] = {base, t};
            ShinerRGB out[3];
            shiner_render(ls, 2, 0, out, 3);
            return rgb(out[1]);
        };
        ShinerLayer l = top; l.animation = 14;
        check("out-of-range animation renders black", render(l) == CRGB(0, 0, 0));
        l.animation = -1;
        check("negative animation renders black", render(l) == CRGB(0, 0, 0));
        l = top; l.animation = 0;
        check("Nothing skips the layer", render(l) == CRGB(255, 0, 0));
        l = top; l.blendMode = 99;
        check("out-of-range blend acts as Add", render(l) == CRGB(255, 255, 0));
        l.blendMode = -1;
        check("negative blend acts as Add", render(l) == CRGB(255, 255, 0));
        l = top; l.gain = 0;
        check("gain 0 leaves the output untouched", render(l) == CRGB(255, 0, 0));
        l.gain = 2;
        check("gain above 1 is 1", render(l) == CRGB(0, 255, 0));
        l.gain = NAN;
        check("NaN gain is 0", render(l) == CRGB(255, 0, 0));
        ShinerRGB untouched = {1, 2, 3};
        shiner_render(&base, 1, 0, &untouched, 0);
        shiner_render(&base, 1, 0, NULL, 3);
        check("no pixels, no writes", untouched.r == 1 && untouched.g == 2 && untouched.b == 3);
        ShinerRGB dark = {9, 9, 9};
        shiner_render(NULL, 3, 0, &dark, 1);
        check("no layers is black", dark.r == 0 && dark.g == 0 && dark.b == 0);
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
