// Host tests for Codecs.h: decode strictness, encode round-trips, clamping.
#include "FastLED.h"
#include "../Codecs.h"

std::vector<String> animationNames = {"Nothing", "Static", "Opposing Waves"};

static int failures = 0;
static void check(const char *what, bool ok) {
    if(!ok) failures++;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

int main() {
    int i; float f; CRGB c; RunMode m; LayerBlendMode bm; LedColorOrder o; String s;

    printf("int\n");
    check("42", Codec<int>::decode("42", i) && i == 42);
    check("-7", Codec<int>::decode("-7", i) && i == -7);
    check("reject banana", !Codec<int>::decode("banana", i));
    check("reject 12abc", !Codec<int>::decode("12abc", i));
    check("reject empty", !Codec<int>::decode("", i));
    check("roundtrip", Codec<int>::encode(42) == "42");

    printf("float\n");
    check("0.8", Codec<float>::decode("0.8", f) && f > 0.79f && f < 0.81f);
    check("reject banana", !Codec<float>::decode("banana", f));
    check("reject nan", !Codec<float>::decode("nan", f));
    check("reject inf", !Codec<float>::decode("inf", f));
    check("encode 3 decimals", Codec<float>::encode(0.125f) == "0.125");
    check("roundtrip decode(encode(x))", Codec<float>::decode(Codec<float>::encode(1.5f), f) && f == 1.5f);

    printf("CRGB\n");
    check("255 100 0", Codec<CRGB>::decode("255 100 0", c) && c == CRGB(255,100,0));
    check("reject 2 fields", !Codec<CRGB>::decode("255 100", c));
    check("reject 4 fields", !Codec<CRGB>::decode("1 2 3 4", c));
    check("reject 256", !Codec<CRGB>::decode("256 0 0", c));
    check("reject negative", !Codec<CRGB>::decode("-1 0 0", c));
    check("reject banana", !Codec<CRGB>::decode("banana", c));
    check("roundtrip", Codec<CRGB>::encode(CRGB(240,255,0)) == "240 255 0");
    check("roundtrip decode(encode)", Codec<CRGB>::decode(Codec<CRGB>::encode(CRGB(1,2,3)), c) && c == CRGB(1,2,3));

    printf("RunMode\n");
    check("0", Codec<RunMode>::decode("0", m) && m == Off);
    check("1", Codec<RunMode>::decode("1", m) && m == On);
    check("reject 2", !Codec<RunMode>::decode("2", m));
    check("reject -1", !Codec<RunMode>::decode("-1", m));

    printf("named enums\n");
    check("blend by name", Codec<LayerBlendMode>::decode("Multiply", bm) && bm == BlendModeMultiply);
    check("blend by index", Codec<LayerBlendMode>::decode("4", bm) && bm == BlendModeMultiply);
    check("blend reject unknown", !Codec<LayerBlendMode>::decode("Bananas", bm));
    check("blend reject out-of-range index", !Codec<LayerBlendMode>::decode("99", bm));
    check("blend roundtrip", Codec<LayerBlendMode>::encode(BlendModeScreen) == "Screen");
    check("order GRB", Codec<LedColorOrder>::decode("GRB", o) && o == LedOrderGRB);
    check("order reject", !Codec<LedColorOrder>::decode("XYZ", o));
    check("animation by name", AnimationCodec::decode("Opposing Waves", i) && i == 2);
    check("animation by index (legacy app)", AnimationCodec::decode("1", i) && i == 1);
    check("animation reject", !AnimationCodec::decode("Fire", i));
    check("animation encode", AnimationCodec::encode(0) == "Nothing");

    printf("clamp\n");
    check("float clamps", clampTo(150.0f, 0.001f, 100.0f) == 100.0f);
    check("float lo", clampTo(0.0f, 0.001f, 100.0f) == 0.001f);
    check("int passthrough", clampTo(128, 0, 255) == 128);

    printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
