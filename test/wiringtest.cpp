// Host tests for Wiring.h: board defaults, the wiring codec and its validation,
// wiringDocs, knob curves, pinStatus and the HSV channel hold. See docs/wiring.md.
#include "FastLED.h"
#include "../ShinyTypes.h"
#include "../Wiring.h"
#include <cmath>
#include <cstring>

static int failures = 0;
static void check(const char *what, bool ok) {
    if(!ok) failures++;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}
static void expect(const char *what, const String &got, const char *want) {
    bool ok = got == want;
    if(!ok) failures++;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) printf("       got  %s\n       want %s\n", got.c_str(), want);
}
static bool near(float a, float b) { return fabsf(a - b) < 0.001f * (1 + fabsf(b)); }

// A write that must be rejected, leaving the wiring untouched, for the reason given.
static void rejects(const BoardSpec &board, const char *what, const char *json) {
    Wiring w = defaultWiring(board), before = w;
    String error;
    bool decoded = decodeWiring(board, json, w, error);
    check(what, !decoded && w == before && !error.isEmpty());
    if(!decoded) printf("       (%s)\n", error.c_str());
}

static const char *kContractExample =
    "{\"G26\":{\"leds\":true},"
    "\"G21\":{\"button\":\"nextPreset\"},"
    "\"G25\":{\"switch\":\"lightsOff\"},"
    "\"G22\":{\"switch\":\"preset\",\"preset\":0},"
    "\"G23\":{\"switch\":\"layerOn\",\"layer\":3},"
    "\"G32\":{\"knob\":\"brightness\",\"min\":0,\"max\":255},"
    "\"G33\":{\"knob\":\"color1.h\",\"layer\":2,\"min\":0,\"max\":255}}";

int main() {
    printf("board defaults encode canonically and round-trip\n");
    {
        struct { const BoardSpec *board; const char *canonical; } cases[] = {
            {&kAtomLite,
             "{\"G21\":{\"button\":\"nextPreset\"},\"G25\":{\"switch\":\"lightsOff\"},"
             "\"G22\":{\"switch\":\"preset\",\"preset\":0},\"G19\":{\"switch\":\"preset\",\"preset\":1},"
             "\"G23\":{\"switch\":\"preset\",\"preset\":2},\"G33\":{\"switch\":\"preset\",\"preset\":3},"
             "\"G26\":{\"leds\":true},\"G32\":{\"leds\":true}}"},
            {&kAtomEcho,
             "{\"G21\":{\"button\":\"nextPreset\"},\"G25\":{\"switch\":\"lightsOff\"},"
             "\"G26\":{\"leds\":true},\"G32\":{\"leds\":true}}"},
            {&kAtomS3Lite,
             "{\"G39\":{\"button\":\"nextPreset\"},\"G38\":{\"switch\":\"lightsOff\"},"
             "\"G5\":{\"switch\":\"preset\",\"preset\":0},\"G6\":{\"switch\":\"preset\",\"preset\":1},"
             "\"G7\":{\"switch\":\"preset\",\"preset\":2},\"G8\":{\"switch\":\"preset\",\"preset\":3},"
             "\"G2\":{\"leds\":true},\"G1\":{\"leds\":true}}"},
        };
        for(auto &c: cases) {
            Wiring w = defaultWiring(*c.board), back;
            String error;
            String encoded = encodeWiring(*c.board, w);
            expect(c.board->id, encoded, c.canonical);
            check("  decode(encode(default)) == default", decodeWiring(*c.board, encoded, back, error) && back == w);
        }
        check("stickc has no wiring pins", encodeWiring(kStickC, defaultWiring(kStickC)) == "{}");
    }

    printf("the contract example\n");
    {
        Wiring w, back;
        String error;
        check("decodes on atom-lite", decodeWiring(kAtomLite, kContractExample, w, error));
        String encoded = encodeWiring(kAtomLite, w);
        expect("canonical on atom-lite", encoded,
               "{\"G21\":{\"button\":\"nextPreset\"},\"G25\":{\"switch\":\"lightsOff\"},"
               "\"G22\":{\"switch\":\"preset\",\"preset\":0},\"G23\":{\"switch\":\"layerOn\",\"layer\":3},"
               "\"G33\":{\"knob\":\"color1.h\",\"layer\":2,\"min\":0,\"max\":255},"
               "\"G26\":{\"leds\":true},\"G32\":{\"knob\":\"brightness\",\"min\":0,\"max\":255}}");
        check("round-trips", decodeWiring(kAtomLite, encoded, back, error) && back == w);
        check("G33 knob decoded", w.pins[5].role == RoleKnob && w.pins[5].target == KnobColor1H &&
                                  w.pins[5].layer == 2 && w.pins[5].max == 255);
        check("led pins are G26 only", w.ledPins() == 0x40);
        rejects(kAtomEcho, "rejected on atom-echo (claimed pins)", kContractExample);
        rejects(kAtomEcho, "G33 alone rejected on atom-echo", "{\"G33\":{\"switch\":\"lightsOff\"}}");
    }

    printf("accepted edge cases\n");
    {
        Wiring w;
        String error;
        check("empty object is all nothing", decodeWiring(kAtomLite, "{}", w, error) && w == Wiring());
        check("conditional claim may hold a role",
              decodeWiring(kAtomLite, "{\"G23\":{\"button\":\"toggleLights\"}}", w, error) && w.pins[4].role == RoleButton);
        check("key order inside a role is free",
              decodeWiring(kAtomS3Lite, "{\"G5\":{\"max\":0.5,\"min\":20,\"layer\":9,\"knob\":\"speed\"}}", w, error));
        expect("reversed float range, shortest numbers", encodeWiring(kAtomS3Lite, w),
               "{\"G5\":{\"knob\":\"speed\",\"layer\":9,\"min\":20,\"max\":0.5}}");
        check("negative tau range", decodeWiring(kAtomS3Lite, "{\"G1\":{\"knob\":\"tau\",\"layer\":0,\"min\":-100,\"max\":0.001}}", w, error));
        expect("  encodes", encodeWiring(kAtomS3Lite, w), "{\"G1\":{\"knob\":\"tau\",\"layer\":0,\"min\":-100,\"max\":0.001}}");
        Wiring back;
        check("  round-trips exactly", decodeWiring(kAtomS3Lite, encodeWiring(kAtomS3Lite, w), back, error) && back == w);
    }

    printf("rejections, one per validation rule\n");
    rejects(kAtomLite, "doesn't parse", "{\"G21\":");
    rejects(kAtomLite, "not an object", "[1,2]");
    rejects(kAtomLite, "key isn't a pin of the board", "{\"G1\":{\"button\":\"nextPreset\"}}");
    rejects(kAtomEcho, "pin has a permanent claim", "{\"G19\":{\"button\":\"nextPreset\"}}");
    rejects(kAtomLite, "knob needs an analog pin", "{\"G21\":{\"knob\":\"brightness\",\"min\":0,\"max\":255}}");
    rejects(kAtomLite, "LEDs need an LED-capable pin", "{\"G21\":{\"leds\":true}}");
    rejects(kAtomLite, "unknown switch action", "{\"G21\":{\"switch\":\"explode\"}}");
    rejects(kAtomLite, "unknown button action", "{\"G21\":{\"button\":\"lightsOff\"}}");
    rejects(kAtomLite, "unknown knob target", "{\"G32\":{\"knob\":\"color3.r\",\"layer\":0,\"min\":0,\"max\":255}}");
    rejects(kAtomLite, "no role key", "{\"G21\":{}}");
    rejects(kAtomLite, "two role keys", "{\"G21\":{\"switch\":\"lightsOff\",\"button\":\"nextPreset\"}}");
    rejects(kAtomLite, "required preset missing", "{\"G21\":{\"switch\":\"preset\"}}");
    rejects(kAtomLite, "required layer missing", "{\"G21\":{\"button\":\"toggleLayer\"}}");
    rejects(kAtomLite, "knob layer missing", "{\"G32\":{\"knob\":\"speed\",\"min\":1,\"max\":2}}");
    rejects(kAtomLite, "knob min missing", "{\"G32\":{\"knob\":\"brightness\",\"max\":255}}");
    rejects(kAtomLite, "preset out of range", "{\"G21\":{\"button\":\"preset\",\"preset\":5}}");
    rejects(kAtomLite, "layer out of range", "{\"G21\":{\"switch\":\"layerOn\",\"layer\":10}}");
    rejects(kAtomLite, "preset not an integer", "{\"G21\":{\"button\":\"preset\",\"preset\":1.5}}");
    rejects(kAtomLite, "unexpected key", "{\"G21\":{\"switch\":\"lightsOff\",\"preset\":0}}");
    rejects(kAtomLite, "layer on brightness is unexpected", "{\"G32\":{\"knob\":\"brightness\",\"layer\":0,\"min\":0,\"max\":255}}");
    rejects(kAtomLite, "leds must be true", "{\"G26\":{\"leds\":false}}");
    rejects(kAtomLite, "min outside the target's range", "{\"G32\":{\"knob\":\"speed\",\"layer\":0,\"min\":0,\"max\":2}}");
    rejects(kAtomLite, "max outside the target's range", "{\"G32\":{\"knob\":\"brightness\",\"min\":0,\"max\":256}}");
    rejects(kAtomLite, "min not a number", "{\"G32\":{\"knob\":\"brightness\",\"min\":\"0\",\"max\":255}}");

    printf("wiringDocs\n");
    expect("atom-echo matches docs/wiring.md", wiringDocsJSON(kAtomEcho),
           "{\"board\":\"atom-echo\","
           "\"pins\":[\"G21\",\"G25\",\"G22\",\"G19\",\"G23\",\"G33\",\"G26\",\"G32\"],"
           "\"analog\":[\"G32\"],"
           "\"led\":[\"G26\",\"G32\"],"
           "\"claims\":{\"G19\":\"speaker\",\"G22\":\"speaker\",\"G23\":\"microphone\",\"G33\":\"microphone\"},"
           "\"targets\":[\"brightness\",\"speed\",\"tau\",\"phi\",\"color1.r\",\"color1.g\",\"color1.b\","
           "\"color1.h\",\"color1.s\",\"color1.v\",\"color2.r\",\"color2.g\",\"color2.b\","
           "\"color2.h\",\"color2.s\",\"color2.v\"]}");
    check("atoms3-lite fits the characteristic", wiringDocsJSON(kAtomS3Lite).length() <= 512);
    check("atoms3-lite has no claims", strstr(wiringDocsJSON(kAtomS3Lite).c_str(), "\"claims\":{}") != nullptr);

    printf("knob curves\n");
    check("brightness 0", near(knobValue(KnobBrightness, 0, 255, 0), 0));
    check("brightness 0.5 is a quarter (square law)", near(knobValue(KnobBrightness, 0, 255, 0.5f), 63.75f));
    check("brightness 1", near(knobValue(KnobBrightness, 0, 255, 1), 255));
    check("brightness reversed 0", near(knobValue(KnobBrightness, 255, 0, 0), 255));
    check("brightness reversed 1", near(knobValue(KnobBrightness, 255, 0, 1), 0));
    check("brightness reversed 0.5", near(knobValue(KnobBrightness, 255, 0, 0.5f), 191.25f));
    check("speed log 0", near(knobValue(KnobSpeed, 0.1f, 10, 0), 0.1f));
    check("speed log 0.5 is the geometric mean", near(knobValue(KnobSpeed, 0.1f, 10, 0.5f), 1));
    check("speed log 1", near(knobValue(KnobSpeed, 0.1f, 10, 1), 10));
    check("speed log reversed 0", near(knobValue(KnobSpeed, 10, 0.1f, 0), 10));
    check("speed log reversed 0.5", near(knobValue(KnobSpeed, 10, 0.1f, 0.5f), 1));
    check("tau through zero is linear", near(knobValue(KnobTau, -10, 10, 0.5f), 0));
    check("phi positive is log", near(knobValue(KnobPhi, 1, 100, 0.5f), 10));
    check("hue linear 0.5", near(knobValue(KnobColor1H, 0, 255, 0.5f), 127.5f));
    check("hue linear reversed 0.25", near(knobValue(KnobColor1H, 255, 0, 0.25f), 191.25f));

    printf("pinStatus\n");
    {
        uint8_t positions[] = {0x9e};
        expect("contract example", encodePinStatus(0x07, 0x01, 0x3c, positions, 1), "07013c9e");
        uint8_t six[] = {0, 1, 2, 3, 0xfe, 0xff};
        String s = encodePinStatus(0xff, 0x80, 0x00, six, 6);
        expect("atoms3-lite width", s, "ff800000010203feff");
        check("fits a notification", s.length() <= 20);
    }

    printf("HSV channel hold\n");
    {
        HsvChannelHold hold;
        CRGB stored(200, 60, 30);
        stored = hold.write(stored, 0, 0);
        CHSV first = hold.hsv;
        for(int i = 1; i <= 100; i++) stored = hold.write(stored, 0, (uint8_t)(i * 37));
        check("saturation held over 100 hue writes", hold.hsv.s == first.s);
        check("value held over 100 hue writes", hold.hsv.v == first.v);
        check("stored color is the held HSV", stored == CRGB(CHSV((uint8_t)(100 * 37), first.s, first.v)));

        CRGB elsewhere(0, 0, 255);
        CRGB out = hold.write(elsewhere, 1, 128);
        check("an outside change is re-derived", hold.hsv.v == 255 && hold.hsv.s == 128 && out == CRGB(CHSV(hold.hsv.h, 128, 255)));
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
